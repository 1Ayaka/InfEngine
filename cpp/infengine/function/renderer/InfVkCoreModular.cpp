/**
 * @file InfVkCoreModular.cpp
 * @brief Implementation of the modular Vulkan core — init, lifecycle, texture/shader, internal
 *
 * Drawing methods → VkCoreDraw.cpp
 * Material/lighting/accessors → VkCoreMaterial.cpp
 */

#include "InfVkCoreModular.h"
#include "InfError.h"
#include "SceneRenderTarget.h"

#include <function/renderer/shader/ShaderProgram.h>
#include <function/resources/InfMaterial/InfMaterial.h>
#include <function/scene/SceneRenderer.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cstring>
#include <limits>

namespace infengine
{

// ============================================================================
// Constructor / Destructor
// ============================================================================

InfVkCoreModular::InfVkCoreModular(int maxFrameInFlight) : m_maxFramesInFlight(static_cast<uint32_t>(maxFrameInFlight))
{
    m_deletionQueue.Initialize(m_maxFramesInFlight);
}

InfVkCoreModular::~InfVkCoreModular()
{
    if (m_deviceContext.IsValid() && !m_shuttingDown) {
        m_deviceContext.WaitIdle();
    }

    // Flush all deferred deletions before tearing down subsystems
    m_deletionQueue.FlushAll();

    if (m_materialPipelineManagerInitialized) {
        m_materialPipelineManager.Shutdown(m_shuttingDown);
        m_materialPipelineManagerInitialized = false;
    }

    // Cleanup shaders — use m_pipelineManager.DestroyShaderModule() which
    // also removes the handle from VkPipelineManager's tracking list,
    // preventing a double-free when Destroy() is called later.
    for (auto &[name, shader] : m_vertShaders) {
        m_pipelineManager.DestroyShaderModule(shader);
    }
    for (auto &[name, shader] : m_fragShaders) {
        m_pipelineManager.DestroyShaderModule(shader);
    }

    // During shutdown the device is already idle (drained once by ~InfRenderer).
    // Tell RAII members to skip their own vkDeviceWaitIdle calls.
    if (m_shuttingDown) {
        m_resourceManager.SetSkipWaitIdle(true);
        m_pipelineManager.SetSkipWaitIdle(true);
        m_swapchain.SetSkipWaitIdle(true);
    }

    // Cleanup shadow pipeline resources before general cleanup
    CleanupShadowPipeline();

    // Cleanup per-view descriptor resources (multi-camera shadow)
    DestroyPerViewDescriptorResources();

    // Explicit destruction in controlled order (avoids double-free from
    // RAII reverse-declaration order when handles are shared across systems).
    m_perObjectBuffers.clear();
    m_textures.clear();
    m_shaderProgramCache.Clear();

    m_vertShaderCodes.clear();
    m_fragShaderCodes.clear();
    m_vertShaders.clear();
    m_fragShaders.clear();

    m_lightingUboBuffers.clear();
    m_lightingUboMapped.clear();
    m_materialUboBuffers.clear();
    m_materialUboMapped.clear();
    m_uniformBuffers.clear();

    m_commandBuffers.clear();
    m_depthImage.reset();

    m_renderGraph.Destroy();
    m_resourceManager.Destroy();

    // RenderGraph::Destroy() and MaterialPipelineManager::Shutdown()
    // already destroyed render passes, pipelines, and descriptor set
    // layouts that VkPipelineManager was tracking.  Clear the stale
    // handles so Destroy() doesn't double-free them.
    m_pipelineManager.ClearTrackedResources();
    m_pipelineManager.Destroy();

    m_swapchain.Destroy();
    m_deviceContext.Destroy();

    // All RAII destructors that fire after this point will find VK_NULL_HANDLE
    // in their stored device and skip Vulkan calls.
}

// ============================================================================
// Initialization
// ============================================================================

void InfVkCoreModular::SetThreads(size_t graphicsThreads, size_t presentThreads, size_t transferThreads,
                                  size_t computeThreads)
{
    // Thread configuration is now handled internally
    // Kept for API compatibility
    INFLOG_INFO("Thread configuration: graphics=", graphicsThreads, ", present=", presentThreads,
                ", transfer=", transferThreads, ", compute=", computeThreads);
}

void InfVkCoreModular::Init(InfAppMetadata appMetaData, InfAppMetadata rendererMetaData, uint32_t vkWindowExtCount,
                            const char **vkWindowExts)
{
    INFLOG_INFO("Initializing InfVkCoreModular...");

    // Configure device (store for use in PrepareSurface)
    m_deviceConfig.appName = appMetaData.appName ? appMetaData.appName : "InfEngine App";
    m_deviceConfig.engineName = rendererMetaData.appName ? rendererMetaData.appName : "InfEngine";
#ifdef NDEBUG
    m_deviceConfig.enableValidationLayers = false;
#else
    m_deviceConfig.enableValidationLayers = true;
#endif

    // Initialize instance only (device will be created in PrepareSurface after surface is available)
    if (!m_deviceContext.InitializeInstance(m_deviceConfig)) {
        INFLOG_ERROR("Failed to initialize Vulkan instance");
        return;
    }

    // Store instance for InfRenderer access
    m_instance = m_deviceContext.GetInstance();

    INFLOG_INFO("InfVkCoreModular instance initialized successfully");
}

void InfVkCoreModular::PrepareSurface()
{
    // Surface should be set by InfRenderer before calling this
    if (m_surface == VK_NULL_HANDLE) {
        INFLOG_ERROR("Surface not set. Call CreateSurface first.");
        return;
    }

    // Complete device initialization with the surface
    if (!m_deviceContext.InitializeDevice(m_surface, m_deviceConfig)) {
        INFLOG_ERROR("Failed to initialize Vulkan device");
        return;
    }

    // Now that device is ready, initialize resource manager
    if (!m_resourceManager.Initialize(m_deviceContext)) {
        INFLOG_ERROR("Failed to initialize resource manager");
        return;
    }

    // Initialize pipeline manager
    m_pipelineManager.Initialize(m_deviceContext.GetDevice());

    // Initialize render graph
    m_renderGraph.Initialize(&m_deviceContext, &m_pipelineManager);

    // Get extent from surface capabilities
    auto swapchainSupport = m_deviceContext.QuerySwapchainSupport();
    uint32_t width = swapchainSupport.capabilities.currentExtent.width;
    uint32_t height = swapchainSupport.capabilities.currentExtent.height;

    if (width == std::numeric_limits<uint32_t>::max() || height == std::numeric_limits<uint32_t>::max() || width == 0 ||
        height == 0) {
        width = (m_windowWidth > 0) ? m_windowWidth : swapchainSupport.capabilities.minImageExtent.width;
        height = (m_windowHeight > 0) ? m_windowHeight : swapchainSupport.capabilities.minImageExtent.height;

        width = std::clamp(width, swapchainSupport.capabilities.minImageExtent.width,
                           swapchainSupport.capabilities.maxImageExtent.width);
        height = std::clamp(height, swapchainSupport.capabilities.minImageExtent.height,
                            swapchainSupport.capabilities.maxImageExtent.height);
    }

    // Create swapchain
    if (!m_swapchain.Create(m_deviceContext, width, height)) {
        INFLOG_ERROR("Failed to create swapchain");
        return;
    }

    // Create depth resources
    CreateDepthResources();

    // Create uniform buffers
    CreateUniformBuffers();

    // Allocate command buffers
    m_commandBuffers.resize(m_maxFramesInFlight);
    for (uint32_t i = 0; i < m_maxFramesInFlight; ++i) {
        auto alloc = m_resourceManager.AllocatePrimaryCommandBuffer();
        m_commandBuffers[i] = alloc.cmdBuffer;
    }

    INFLOG_INFO("Surface prepared successfully");
}

void InfVkCoreModular::PreparePipeline()
{
    // Create default white texture
    CreateDefaultWhiteTexture("white");

    // Create default flat normal texture (0.5, 0.5, 1.0 = tangent-space (0,0,1))
    {
        auto normalTex = m_resourceManager.CreateSolidColorTexture(1, 1, 128, 128, 255, 255, VK_FORMAT_R8G8B8A8_UNORM);
        if (normalTex) {
            m_textures["_default_normal"] = std::move(normalTex);
            INFLOG_INFO("Created default flat normal texture: _default_normal");
        }
    }

    // Initialize material system (default material + pipelines)
    InitializeMaterialSystem();

    // Create per-view descriptor set layout and pool (multi-camera shadow isolation).
    // Must be after InitializeMaterialSystem so default textures are available.
    CreatePerViewDescriptorResources();

    INFLOG_INFO("Pipeline prepared successfully");
}

// ============================================================================
// Texture Management
// ============================================================================

void InfVkCoreModular::CreateTextureImage(std::string name, std::string path)
{
    auto texture = m_resourceManager.LoadTexture(path);
    if (texture) {
        m_textures[name] = std::move(texture);
        INFLOG_INFO("Loaded texture: ", name);
    }
}

void InfVkCoreModular::CreateDefaultWhiteTexture(std::string name)
{
    auto texture = m_resourceManager.CreateSolidColorTexture(1, 1, 255, 255, 255, 255);
    if (texture) {
        m_textures[name] = std::move(texture);
        INFLOG_INFO("Created default white texture: ", name);
    }
}

void InfVkCoreModular::LoadTexture(const std::string &name, const std::string &path)
{
    CreateTextureImage(name, path);
}

// ============================================================================
// Shader and Pipeline Management
// ============================================================================

void InfVkCoreModular::LoadShader(const char *name, const std::vector<char> &spirvCode, const char *type)
{
    // Convert char vector to uint32_t vector
    std::vector<uint32_t> code(spirvCode.size() / sizeof(uint32_t));
    std::memcpy(code.data(), spirvCode.data(), spirvCode.size());

    VkShaderModule module = m_pipelineManager.CreateShaderModule(code);
    if (module == VK_NULL_HANDLE) {
        INFLOG_ERROR("Failed to load shader: ", name);
        return;
    }

    std::string typeStr(type);
    if (typeStr == "vert" || typeStr == "vertex") {
        m_vertShaders[name] = module;
        m_vertShaderCodes[name] = spirvCode;
    } else if (typeStr == "frag" || typeStr == "fragment") {
        m_fragShaders[name] = module;
        m_fragShaderCodes[name] = spirvCode;
    } else {
        INFLOG_WARN("Unknown shader type: ", type);
        m_pipelineManager.DestroyShaderModule(module);
    }
}

void InfVkCoreModular::UnloadShader(const char *name)
{
    std::string nameStr(name);

    auto vertIt = m_vertShaders.find(nameStr);
    if (vertIt != m_vertShaders.end()) {
        vkDestroyShaderModule(GetDevice(), vertIt->second, nullptr);
        m_vertShaders.erase(vertIt);
    }
    m_vertShaderCodes.erase(nameStr);

    auto fragIt = m_fragShaders.find(nameStr);
    if (fragIt != m_fragShaders.end()) {
        vkDestroyShaderModule(GetDevice(), fragIt->second, nullptr);
        m_fragShaders.erase(fragIt);
    }
    m_fragShaderCodes.erase(nameStr);
}

bool InfVkCoreModular::HasShader(const std::string &name, const std::string &type) const
{
    if (type == "vert" || type == "vertex") {
        return m_vertShaders.find(name) != m_vertShaders.end();
    }
    if (type == "frag" || type == "fragment") {
        return m_fragShaders.find(name) != m_fragShaders.end();
    }
    return false;
}

void InfVkCoreModular::InvalidateShaderCache(const std::string &shaderId)
{
    INFLOG_INFO("Invalidating shader cache for: ", shaderId);

    // Wait for GPU to finish any pending work using this shader
    m_deviceContext.WaitIdle();

    // Remove shader programs from cache that contain this shader
    // This handles the case where shaderId is a simple name like "123"
    // but cache keys are like "shaders/default.vert|shaders/123.frag"
    m_shaderProgramCache.RemoveProgramsContainingShader(shaderId);

    // Invalidate all materials using this shader in MaterialPipelineManager
    if (m_materialPipelineManagerInitialized) {
        m_materialPipelineManager.InvalidateMaterialsUsingShader(shaderId);
    }

    // Also unload the shader module so it gets recreated
    UnloadShader(shaderId.c_str());

    INFLOG_INFO("Shader cache invalidated for: ", shaderId);
}

// ============================================================================
// Command Buffer Utilities
// ============================================================================

VkCommandBuffer InfVkCoreModular::BeginSingleTimeCommands()
{
    return m_resourceManager.BeginSingleTimeCommands();
}

void InfVkCoreModular::EndSingleTimeCommands(VkCommandBuffer commandBuffer)
{
    m_resourceManager.EndSingleTimeCommands(commandBuffer);
}

// ============================================================================
// Render Callbacks (RenderGraph-based)
// ============================================================================

void InfVkCoreModular::SetRenderGraphExecutor(std::function<void(VkCommandBuffer cmdBuf)> executor)
{
    m_renderGraphExecutor = std::move(executor);
}

void InfVkCoreModular::SetGuiRenderCallback(std::function<void(vk::RenderContext &ctx)> callback)
{
    m_guiRenderCallback = std::move(callback);
}

// ============================================================================
// Internal Methods
// ============================================================================

void InfVkCoreModular::RecreateSwapchain()
{
    // Wait for device idle
    m_deviceContext.WaitIdle();

    // Cleanup old depth resources
    m_depthImage.reset();

    // Get new extent from surface capabilities
    auto swapchainSupport = m_deviceContext.QuerySwapchainSupport();
    uint32_t width = swapchainSupport.capabilities.currentExtent.width;
    uint32_t height = swapchainSupport.capabilities.currentExtent.height;

    if (width == std::numeric_limits<uint32_t>::max() || height == std::numeric_limits<uint32_t>::max() || width == 0 ||
        height == 0) {
        width = (m_windowWidth > 0) ? m_windowWidth : swapchainSupport.capabilities.minImageExtent.width;
        height = (m_windowHeight > 0) ? m_windowHeight : swapchainSupport.capabilities.minImageExtent.height;

        width = std::clamp(width, swapchainSupport.capabilities.minImageExtent.width,
                           swapchainSupport.capabilities.maxImageExtent.width);
        height = std::clamp(height, swapchainSupport.capabilities.minImageExtent.height,
                            swapchainSupport.capabilities.maxImageExtent.height);
    }

    // Handle special case (some window managers report invalid extents)
    if (width == 0 || height == 0) {
        // Cannot recreate, wait for valid extent
        return;
    }

    // Recreate swapchain
    m_swapchain.Recreate(m_deviceContext, width, height);

    // Recreate depth resources
    CreateDepthResources();
}

void InfVkCoreModular::CreateDepthResources()
{
    VkExtent2D extent = m_swapchain.GetExtent();
    VkFormat depthFormat = m_deviceContext.FindDepthFormat();

    m_depthImage = m_resourceManager.CreateDepthBuffer(extent.width, extent.height, depthFormat);
}

void InfVkCoreModular::CreateUniformBuffers()
{
    VkDeviceSize bufferSize = sizeof(UniformBufferObject);

    m_uniformBuffers.resize(m_maxFramesInFlight);
    for (size_t i = 0; i < m_maxFramesInFlight; ++i) {
        m_uniformBuffers[i] = m_resourceManager.CreateUniformBuffer(bufferSize);
    }

    // Create lighting UBO buffers (binding 1)
    VkDeviceSize lightingUboSize = sizeof(ShaderLightingUBO);
    m_lightingUboBuffers.resize(m_maxFramesInFlight);
    m_lightingUboMapped.resize(m_maxFramesInFlight, nullptr);

    for (size_t i = 0; i < m_maxFramesInFlight; ++i) {
        m_lightingUboBuffers[i] = m_resourceManager.CreateUniformBuffer(lightingUboSize);
        if (m_lightingUboBuffers[i]) {
            m_lightingUboMapped[i] = m_lightingUboBuffers[i]->Map(0, lightingUboSize);
            if (m_lightingUboMapped[i]) {
                // Initialize with default ambient lighting (neutral white)
                ShaderLightingUBO defaultLighting{};
                defaultLighting.lightCounts = glm::ivec4(0, 0, 0, 0);
                defaultLighting.ambientColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.3f); // White ambient, low intensity
                defaultLighting.cameraPos = glm::vec4(0.0f, 0.0f, 5.0f, 1.0f);
                std::memcpy(m_lightingUboMapped[i], &defaultLighting, lightingUboSize);
            }
        }
    }
    INFLOG_INFO("Created lighting UBO buffers: ", lightingUboSize, " bytes x ", m_maxFramesInFlight, " frames");

    // Create default material UBO buffers (binding 2)
    size_t materialUboSize = InfMaterial::GetUBOSize();
    m_materialUboBuffers.resize(m_maxFramesInFlight);
    m_materialUboMapped.resize(m_maxFramesInFlight, nullptr);

    for (size_t i = 0; i < m_maxFramesInFlight; ++i) {
        m_materialUboBuffers[i] = m_resourceManager.CreateUniformBuffer(materialUboSize);
        if (m_materialUboBuffers[i]) {
            m_materialUboMapped[i] = m_materialUboBuffers[i]->Map(0, materialUboSize);
            if (m_materialUboMapped[i]) {
                std::memset(m_materialUboMapped[i], 0, materialUboSize);
            }
        }
    }
}

void InfVkCoreModular::RecordCommandBuffer(uint32_t imageIndex)
{
    VkCommandBuffer cmdBuf = m_commandBuffers[m_currentFrame];

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    if (vkBeginCommandBuffer(cmdBuf, &beginInfo) != VK_SUCCESS) {
        INFLOG_ERROR("Failed to begin recording command buffer");
        return;
    }

    // ========================================================================
    // Inline UBO Updates (Fix 1: replaces CPU-side memcpy)
    //
    // Push staged VP and lighting data into the GPU buffers via
    // vkCmdUpdateBuffer.  This is properly serialized within the command
    // buffer's execution timeline, eliminating the CPU→GPU race that
    // previously required vkDeviceWaitIdle() for correctness.
    // ========================================================================
    if (m_uboDirty) {
        CmdUpdateUniformBuffer(cmdBuf, m_stagedUBO.view, m_stagedUBO.proj);
        m_uboDirty = false;
    }
    CmdUpdateLightingUBO(cmdBuf);
    CmdUpdateShadowUBO(cmdBuf);

    // ========================================================================
    // Execute scene render graph (offscreen scene rendering)
    // Shadow passes are now integrated into the scene render graph DAG.
    // ========================================================================
    if (m_renderGraphExecutor) {
        m_renderGraphExecutor(cmdBuf);
    }

    // ========================================================================
    // Post-Scene-Render Callback (OutlineRenderer injection point)
    // ========================================================================
    // The callback is responsible for:
    //   - Rendering outline (mask + composite) when an object is selected
    //   - Transitioning scene color to SHADER_READ_ONLY_OPTIMAL for ImGui
    // If no callback is set, no post-scene processing occurs.
    if (m_postSceneRenderCallback) {
        m_postSceneRenderCallback(cmdBuf, m_drawCalls);
    }

    // ========================================================================
    // Swapchain GUI Pass via RenderGraph
    // ========================================================================
    // Reset and rebuild render graph for this frame's swapchain
    m_renderGraph.Reset();

    // Import swapchain image as backbuffer
    VkImage swapchainImage = m_swapchain.GetImage(imageIndex);
    VkImageView swapchainView = m_swapchain.GetImageView(imageIndex);
    VkExtent2D extent = m_swapchain.GetExtent();
    VkFormat format = m_swapchain.GetImageFormat();

    vk::ResourceHandle backbuffer =
        m_renderGraph.SetBackbuffer(swapchainImage, swapchainView, format, extent.width, extent.height);

    // Capture GUI callback for lambda
    auto guiCallback = m_guiRenderCallback;

    // Add GUI pass to render graph
    m_renderGraph.AddPass("GUI", [backbuffer, extent, guiCallback](vk::PassBuilder &builder) {
        // Write to swapchain backbuffer
        builder.WriteColor(backbuffer, 0);
        builder.SetRenderArea(extent.width, extent.height);
        builder.SetClearColor(0.0f, 0.0f, 0.0f, 1.0f);

        // Return execute callback
        return [guiCallback, extent](vk::RenderContext &ctx) {
            // Set viewport and scissor
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(extent.width);
            viewport.height = static_cast<float>(extent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            ctx.SetViewport(viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = extent;
            ctx.SetScissor(scissor);

            // Execute GUI render callback
            if (guiCallback) {
                guiCallback(ctx);
            }
        };
    });

    // Set output
    m_renderGraph.SetOutput(backbuffer);

    // Compile and execute
    if (!m_renderGraph.Compile()) {
        INFLOG_ERROR("Failed to compile swapchain render graph");
        vkEndCommandBuffer(cmdBuf);
        return;
    }

    m_renderGraph.Execute(cmdBuf);

    if (vkEndCommandBuffer(cmdBuf) != VK_SUCCESS) {
        INFLOG_ERROR("Failed to record command buffer");
    }
}

void InfVkCoreModular::UpdateUniformBuffer(uint32_t currentImage, const float *viewPos, const float *viewLookAt,
                                           const float *viewUp)
{
    UniformBufferObject ubo{};
    ubo.model = glm::mat4(1.0f); // Identity - actual model matrices are per-object

    // Use camera's actual matrices from SceneRenderBridge for consistency with picking
    SceneRenderBridge &bridge = SceneRenderBridge::Instance();
    Camera *activeCamera = bridge.GetActiveCamera();

    if (activeCamera) {
        // Use camera's actual view and projection matrices
        ubo.view = activeCamera->GetViewMatrix();
        ubo.proj = activeCamera->GetProjectionMatrix(); // Already has Y-flip for Vulkan
    } else {
        // Fallback to legacy calculation
        glm::vec3 eye(viewPos[0], viewPos[1], viewPos[2]);
        glm::vec3 center(viewLookAt[0], viewLookAt[1], viewLookAt[2]);
        glm::vec3 up(viewUp[0], viewUp[1], viewUp[2]);
        ubo.view = glm::lookAt(eye, center, up);

        VkExtent2D extent = m_swapchain.GetExtent();
        float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
        if (m_sceneRenderTargetWidth > 0 && m_sceneRenderTargetHeight > 0) {
            aspect = static_cast<float>(m_sceneRenderTargetWidth) / static_cast<float>(m_sceneRenderTargetHeight);
        }
        ubo.proj = glm::perspective(glm::radians(60.0f), aspect, 0.01f, 1000.0f);
        ubo.proj[1][1] *= -1; // Flip Y for Vulkan
    }

    // Stage the UBO data — the actual GPU write happens inline in the
    // command buffer via CmdUpdateUniformBuffer() during RecordCommandBuffer().
    // This eliminates the CPU→GPU race on m_uniformBuffers[0] that previously
    // required vkDeviceWaitIdle() for correctness.
    m_stagedUBO = ubo;
    m_uboDirty = true;
}

// ============================================================================
// Frame Synchronization & Deferred Deletion
// ============================================================================

void InfVkCoreModular::WaitForCurrentFrame()
{
    m_swapchain.WaitForFrame();
}

void InfVkCoreModular::TickDeletionQueue()
{
    m_deletionQueue.Tick();
}

void InfVkCoreModular::DeferDeletion(std::function<void()> deleter)
{
    m_deletionQueue.Push(std::move(deleter));
}

} // namespace infengine
