/**
 * @file InfVkCoreModular.h
 * @brief Modern modular Vulkan core using the new RAII-based architecture
 *
 * This file provides a drop-in replacement for InfVkCore that uses the new
 * modular architecture. It maintains API compatibility with the original
 * InfVkCore while internally using:
 *
 * - VkDeviceContext for device management
 * - VkSwapchainManager for swapchain lifecycle
 * - VkPipelineManager for pipeline/shader management
 * - VkResourceManager for resource creation
 * - RenderGraph for declarative rendering (optional)
 *
 * Migration Guide:
 * 1. Include this header instead of InfVkCore.h
 * 2. Change InfVkCore to InfVkCoreModular
 * 3. Optionally use RenderGraph for declarative rendering
 *
 * Example:
 *   // Old code:
 *   InfVkCore core;
 *   core.Init(...);
 *   core.DrawFrame(...);
 *
 *   // New code:
 *   InfVkCoreModular core;
 *   core.Init(...);
 *   core.DrawFrame(...);  // Same API!
 *
 *   // Or use RenderGraph:
 *   auto& graph = core.GetRenderGraph();
 *   graph.AddPass("MyPass", [](PassBuilder& builder) { ... });
 */

#pragma once

#include "FrameDeletionQueue.h"
#include "InfRenderStruct.h"
#include "MaterialPipelineManager.h"
#include "vk/VkCore.h"
#include <core/types/InfApplication.h>
#include <function/scene/LightingData.h>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Window;

namespace infengine
{

class EditorGizmos;
class InfMaterial;
class SceneRenderTarget;
struct RenderState;

/**
 * @brief Modern modular Vulkan core with RAII resource management
 *
 * This class provides the same interface as InfVkCore but uses
 * the new modular Vulkan architecture internally.
 */
class InfVkCoreModular
{
  public:
    friend class InfGUI;
    friend class InfRenderer;

    /**
     * @brief Construct with specified max frames in flight
     * @param maxFrameInFlight Maximum concurrent frames (default: 2)
     */
    explicit InfVkCoreModular(int maxFrameInFlight = 2);
    ~InfVkCoreModular();

    // Non-copyable, non-movable (like original InfVkCore)
    InfVkCoreModular(const InfVkCoreModular &) = delete;
    InfVkCoreModular &operator=(const InfVkCoreModular &) = delete;
    InfVkCoreModular(InfVkCoreModular &&) = delete;
    InfVkCoreModular &operator=(InfVkCoreModular &&) = delete;

    /// @brief When true, subsystem destructors should skip their individual
    ///        vkDeviceWaitIdle calls — the caller already did a single drain.
    void SetShuttingDown(bool v)
    {
        m_shuttingDown = v;
        m_deviceContext.SetShuttingDown(v);
    }
    bool IsShuttingDown() const
    {
        return m_shuttingDown;
    }

    // ========================================================================
    // Initialization (API compatible with InfVkCore)
    // ========================================================================

    /**
     * @brief Set thread counts for different queue families
     */
    void SetThreads(size_t graphicsThreads, size_t presentThreads, size_t transferThreads, size_t computeThreads);

    /**
     * @brief Initialize Vulkan core
     *
     * @param appMetaData Application metadata
     * @param rendererMetaData Renderer metadata
     * @param vkWindowExtCount Number of window extensions
     * @param vkWindowExts Window extension names
     */
    void Init(InfAppMetadata appMetaData, InfAppMetadata rendererMetaData, uint32_t vkWindowExtCount,
              const char **vkWindowExts);

    /**
     * @brief Prepare surface for rendering
     */
    void PrepareSurface();

    /**
     * @brief Prepare graphics pipeline
     */
    void PreparePipeline();

    /// @brief Set window size for swapchain extent fallback
    void SetWindowSize(uint32_t width, uint32_t height)
    {
        m_windowWidth = width;
        m_windowHeight = height;
    }

    // ========================================================================
    // Texture Management
    // ========================================================================

    void CreateTextureImage(std::string name, std::string path);
    void CreateDefaultWhiteTexture(std::string name);
    void LoadTexture(const std::string &name, const std::string &path);

    // ========================================================================
    // Shader and Pipeline Management
    // ========================================================================

    void LoadShader(const char *name, const std::vector<char> &spirvCode, const char *type);
    void UnloadShader(const char *name);
    bool HasShader(const std::string &name, const std::string &type) const;

    /**
     * @brief Invalidate shader program cache for hot-reload
     *
     * Must be called before loading updated shader code to force pipeline recreation.
     * @param shaderId The shader identifier to invalidate
     */
    void InvalidateShaderCache(const std::string &shaderId);

    // ========================================================================
    // Rendering
    // ========================================================================

    /**
     * @brief Draw a frame
     *
     * @param viewPos Camera position
     * @param viewLookAt Camera look-at point
     * @param viewUp Camera up vector
     */
    void DrawFrame(const float *viewPos, const float *viewLookAt, const float *viewUp);

    /// @brief Draw scene with multiple materials (Unity-style per-object rendering)
    void DrawSceneMultiMaterial(VkCommandBuffer cmdBuf, uint32_t width, uint32_t height);

    /**
     * @brief Draw scene objects filtered by render queue range (Phase 2)
     *
     * Like DrawSceneMultiMaterial but only renders draw calls whose material
     * render queue falls within [queueMin, queueMax]. Used by Python-driven
     * RenderGraph passes to split rendering into multiple passes.
     *
     * @param cmdBuf Vulkan command buffer
     * @param width  Render target width
     * @param height Render target height
     * @param queueMin Minimum render queue (inclusive)
     * @param queueMax Maximum render queue (inclusive)
     */
    void DrawSceneFiltered(VkCommandBuffer cmdBuf, uint32_t width, uint32_t height, int queueMin, int queueMax);

    /**
     * @brief Draw shadow casters into a depth-only shadow map.
     *
     * Uses a dedicated shadow pipeline (shadow.vert/shadow.frag) with front-face
     * culling and depth bias. The light VP is obtained from SceneLightCollector.
     * The shadow pipeline is created lazily on first use.
     *
     * @param cmdBuf Vulkan command buffer (inside a render pass)
     * @param width  Shadow map width
     * @param height Shadow map height
     * @param queueMin Minimum render queue (inclusive)
     * @param queueMax Maximum render queue (inclusive)
     */
    void DrawShadowCasters(VkCommandBuffer cmdBuf, uint32_t width, uint32_t height, int queueMin, int queueMax);

    /// @brief Set draw calls for multi-material rendering
    void SetDrawCalls(const std::vector<DrawCall> &drawCalls);

    /// @brief Update material UBO with current material properties (stub)
    void UpdateMaterialUBO(InfMaterial &material);

    /// @brief Ensure a material has its own UBO buffer allocated (stub)
    void EnsureMaterialUBO(std::shared_ptr<InfMaterial> material);

    /// @brief Ensure per-object GPU buffers exist and match the given mesh data.
    /// Creates new buffers or recreates if vertex/index count changed.
    void EnsureObjectBuffers(uint64_t objectId, const std::vector<Vertex> &vertices,
                             const std::vector<uint32_t> &indices, bool forceUpdate = false);

    /// @brief Remove per-object buffers for objects that are no longer active.
    /// Call once per frame after SetDrawCalls with the current active draw calls.
    void CleanupUnusedBuffers(const std::vector<DrawCall> &activeDrawCalls);

    // ========================================================================
    // Command Buffer Utilities
    // ========================================================================

    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

    // ========================================================================
    // Render Callbacks (RenderGraph-based)
    // ========================================================================

    /// @brief Set the render graph execution callback (offscreen/pre-render)
    void SetRenderGraphExecutor(std::function<void(VkCommandBuffer cmdBuf)> executor);

    /// @brief Set the GUI render callback using RenderGraph context
    /// @param callback Callback that receives RenderContext for drawing
    void SetGuiRenderCallback(std::function<void(vk::RenderContext &ctx)> callback);

    // ========================================================================
    // New Modular API Access
    // ========================================================================

    /**
     * @brief Get the device context for advanced operations
     */
    [[nodiscard]] vk::VkDeviceContext &GetDeviceContext()
    {
        return m_deviceContext;
    }
    [[nodiscard]] const vk::VkDeviceContext &GetDeviceContext() const
    {
        return m_deviceContext;
    }

    /**
     * @brief Get the swapchain manager
     */
    [[nodiscard]] vk::VkSwapchainManager &GetSwapchain()
    {
        return m_swapchain;
    }
    [[nodiscard]] const vk::VkSwapchainManager &GetSwapchain() const
    {
        return m_swapchain;
    }

    /**
     * @brief Get the pipeline manager
     */
    [[nodiscard]] vk::VkPipelineManager &GetPipelineManager()
    {
        return m_pipelineManager;
    }
    [[nodiscard]] const vk::VkPipelineManager &GetPipelineManager() const
    {
        return m_pipelineManager;
    }

    /**
     * @brief Get the resource manager
     */
    [[nodiscard]] vk::VkResourceManager &GetResourceManager()
    {
        return m_resourceManager;
    }
    [[nodiscard]] const vk::VkResourceManager &GetResourceManager() const
    {
        return m_resourceManager;
    }

    /**
     * @brief Get the render graph for declarative rendering
     */
    [[nodiscard]] vk::RenderGraph &GetRenderGraph()
    {
        return m_renderGraph;
    }
    [[nodiscard]] const vk::RenderGraph &GetRenderGraph() const
    {
        return m_renderGraph;
    }

    // ========================================================================
    // Direct Vulkan Access (for compatibility)
    // ========================================================================

    [[nodiscard]] VkDevice GetDevice() const
    {
        return m_deviceContext.GetDevice();
    }
    [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const
    {
        return m_deviceContext.GetPhysicalDevice();
    }
    [[nodiscard]] VkInstance GetInstance() const
    {
        return m_deviceContext.GetInstance();
    }
    [[nodiscard]] VkQueue GetGraphicsQueue() const
    {
        return m_deviceContext.GetGraphicsQueue();
    }
    [[nodiscard]] VkQueue GetPresentQueue() const
    {
        return m_deviceContext.GetPresentQueue();
    }
    [[nodiscard]] uint32_t GetSwapchainImageCount() const
    {
        return m_swapchain.GetImageCount();
    }
    [[nodiscard]] VkCommandPool GetCommandPool() const
    {
        return m_resourceManager.GetCommandPool();
    }
    [[nodiscard]] VkFormat GetSwapchainFormat() const
    {
        return m_swapchain.GetImageFormat();
    }
    [[nodiscard]] VkExtent2D GetSwapchainExtent() const
    {
        return m_swapchain.GetExtent();
    }

    // ========================================================================
    // Scene Render Target / Editor Integration
    // ========================================================================

    /// @brief Set scene render target dimensions for aspect ratio calculation
    void SetSceneRenderTargetSize(uint32_t width, uint32_t height)
    {
        m_sceneRenderTargetWidth = width;
        m_sceneRenderTargetHeight = height;
    }

    /// @brief Set editor gizmos for rendering
    void SetEditorGizmos(EditorGizmos *gizmos)
    {
        m_editorGizmos = gizmos;
    }

    /// @brief Refresh a material's pipeline (placeholder)
    bool RefreshMaterialPipeline(std::shared_ptr<InfMaterial> material, const std::string &vertShaderPath,
                                 const std::string &fragShaderPath);

    /// @brief Initialize material system (default material, pipelines)
    void InitializeMaterialSystem();

    /// @brief Re-initialize the material pipeline manager with a new MSAA sample count.
    /// Must be called after vkDeviceWaitIdle; destroys all cached pipelines.
    void ReinitializeMaterialPipelines(VkSampleCountFlagBits newSampleCount);

    // ========================================================================
    // Pre/Post-Scene-Render Callbacks
    // ========================================================================

    using PostSceneRenderCallback = std::function<void(VkCommandBuffer cmdBuf, const std::vector<DrawCall> &drawCalls)>;

    /// @brief Set callback invoked after scene rendering, before GUI.
    /// Used by InfRenderer to inject OutlineRenderer commands.
    void SetPostSceneRenderCallback(PostSceneRenderCallback callback)
    {
        m_postSceneRenderCallback = std::move(callback);
    }

    // ========================================================================
    // Buffer Accessors (for OutlineRenderer)
    // ========================================================================

    /// @brief Get per-object vertex buffer VkBuffer handle (VK_NULL_HANDLE if not found)
    [[nodiscard]] VkBuffer GetObjectVertexBuffer(uint64_t objectId) const;

    /// @brief Get per-object index buffer VkBuffer handle (VK_NULL_HANDLE if not found)
    [[nodiscard]] VkBuffer GetObjectIndexBuffer(uint64_t objectId) const;

    /// @brief Get uniform buffer VkBuffer at given index (for descriptor set binding)
    [[nodiscard]] VkBuffer GetUniformBuffer(size_t index) const;

    /// @brief Get shader module by name and type ("vertex" or "fragment")
    [[nodiscard]] VkShaderModule GetShaderModule(const std::string &name, const std::string &type) const;

    /// @brief Get the shadow depth sampler (used by per-view shadow descriptors)
    [[nodiscard]] VkSampler GetShadowDepthSampler() const
    {
        return m_shadowDepthSampler;
    }

    // ========================================================================
    // Per-View Descriptor Set (set 1) — multi-camera shadow isolation
    // ========================================================================

    /// @brief Get the per-view descriptor set layout (set 1: binding 0 = shadow map sampler).
    /// Used by SceneRenderGraph to allocate per-graph descriptor sets.
    [[nodiscard]] VkDescriptorSetLayout GetPerViewDescSetLayout() const
    {
        return m_perViewDescSetLayout;
    }

    /// @brief Allocate a per-view descriptor set from the shared pool.
    /// Each SceneRenderGraph calls this once during initialization.
    [[nodiscard]] VkDescriptorSet AllocatePerViewDescriptorSet();

    /// @brief Update a per-view descriptor set with shadow map resources.
    void UpdatePerViewShadowMap(VkDescriptorSet perViewDescSet, VkImageView shadowView, VkSampler shadowSampler);

    /// @brief Clear a per-view descriptor set (bind default white texture).
    void ClearPerViewShadowMap(VkDescriptorSet perViewDescSet);

    /// @brief Set the active per-view descriptor set for subsequent draw calls.
    void SetActiveShadowDescriptorSet(VkDescriptorSet descSet)
    {
        m_activeShadowDescSet = descSet;
    }

    /// @brief Get the active per-view descriptor set (set 1) for draw calls.
    [[nodiscard]] VkDescriptorSet GetActiveShadowDescriptorSet() const
    {
        return m_activeShadowDescSet;
    }

    // ========================================================================
    // Lighting System
    // ========================================================================

    /// @brief Get the scene light collector for ambient/fog settings
    [[nodiscard]] SceneLightCollector &GetLightCollector()
    {
        return m_lightCollector;
    }

    /// @brief Get material pipeline manager
    [[nodiscard]] MaterialPipelineManager &GetMaterialPipelineManager()
    {
        return m_materialPipelineManager;
    }

    /// @brief Set ambient color (convenience method)
    void SetAmbientColor(const glm::vec3 &color, float intensity = 1.0f);

    /// @brief Update lighting UBO for current frame
    void UpdateLightingUBO(const glm::vec3 &cameraPosition);

    // ========================================================================
    // Frame Synchronization & Deferred Deletion
    // ========================================================================

    /// @brief Wait for the current frame-in-flight fence to signal.
    ///
    /// Must be called BEFORE any CPU-side resource mutation that could
    /// conflict with in-flight GPU work for this frame slot.
    void WaitForCurrentFrame();

    /// @brief Tick the deferred deletion queue.
    ///
    /// Flushes entries that are old enough (>= maxFramesInFlight frames)
    /// to guarantee no in-flight command buffer references them.
    /// Call once per frame AFTER WaitForCurrentFrame().
    void TickDeletionQueue();

    /// @brief Enqueue a GPU resource for deferred deletion.
    ///
    /// The deleter lambda will be invoked after maxFramesInFlight frames,
    /// when all in-flight command buffers that might reference the resource
    /// have completed.
    void DeferDeletion(std::function<void()> deleter);

    /// @brief Inline-update the lighting UBO in a command buffer.
    ///
    /// Uses vkCmdUpdateBuffer with proper pipeline barriers so that all
    /// subsequent rendering commands see the updated lighting data.
    /// Called from RecordCommandBuffer() instead of CPU-side memcpy.
    void CmdUpdateLightingUBO(VkCommandBuffer cmdBuf);

    /// @brief Inline-update ONLY the cameraPos field of the lighting UBO.
    ///
    /// Used to override the camera position for per-view rendering (e.g.
    /// Game View uses the game camera while Scene View uses the editor camera)
    /// without re-uploading the full 6 KB lighting UBO.
    void CmdUpdateLightingCameraPos(VkCommandBuffer cmdBuf, const glm::vec3 &cameraPos);

    /// @brief Cache the lighting UBO data for inline command-buffer update.
    ///
    /// Called on the CPU timeline (before DrawFrame). The cached data is
    /// pushed to the GPU via CmdUpdateLightingUBO during command recording.
    void StageLightingUBO(const glm::vec3 &cameraPosition);

    /// @brief Inline-update the per-frame shadow UBO in a command buffer.
    ///
    /// Uses vkCmdUpdateBuffer with explicit barriers so shadow VP updates are
    /// serialized on the GPU timeline (driver-robust across vendors).
    void CmdUpdateShadowUBO(VkCommandBuffer cmdBuf);

  private:
    // ========================================================================
    // Internal Methods
    // ========================================================================

    void RecreateSwapchain();
    void CreateDepthResources();

    void CreateUniformBuffers();
    void RecordCommandBuffer(uint32_t imageIndex);
    void UpdateUniformBuffer(uint32_t currentImage, const float *viewPos, const float *viewLookAt, const float *viewUp);

    /// @brief Update the VP UBO inline in a command buffer (for multi-camera rendering).
    /// Uses vkCmdUpdateBuffer with proper barriers so each render graph sees its own VP matrices.
    void CmdUpdateUniformBuffer(VkCommandBuffer cmdBuf, const glm::mat4 &view, const glm::mat4 &proj);

    /// @brief Create a raw Vulkan buffer via VMA
    void CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer &buffer,
                      VmaAllocation &allocation);

    // ========================================================================
    // New Modular Components
    // ========================================================================

    vk::VkDeviceContext m_deviceContext;
    vk::VkSwapchainManager m_swapchain;
    vk::VkPipelineManager m_pipelineManager;
    vk::VkResourceManager m_resourceManager;
    vk::RenderGraph m_renderGraph;

    // ========================================================================
    // Configuration
    // ========================================================================

    vk::DeviceConfig m_deviceConfig;
    uint32_t m_maxFramesInFlight;
    uint32_t m_currentFrame = 0;
    bool m_framebufferResized = false;

    // Window size fallback (used when surface extent is undefined)
    uint32_t m_windowWidth = 0;
    uint32_t m_windowHeight = 0;

    // ========================================================================
    // Legacy Resources (for API compatibility)
    // ========================================================================

  public:
    // These are exposed for InfRenderer compatibility (friend class can access)
    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;

  private:
    // Scene render target dimensions for aspect ratio calculation
    uint32_t m_sceneRenderTargetWidth = 0;
    uint32_t m_sceneRenderTargetHeight = 0;

    // Material system state
    bool m_materialSystemInitialized = false;
    bool m_materialPipelineManagerInitialized = false;
    VkSampleCountFlagBits m_msaaSampleCount = VK_SAMPLE_COUNT_4_BIT;

    // Shutdown coordination — set by InfRenderer before destroying subsystems
    bool m_shuttingDown = false;

    // Editor gizmos
    EditorGizmos *m_editorGizmos = nullptr;

    // Depth resources
    std::unique_ptr<vk::VkImageHandle> m_depthImage;

    // Command buffers
    std::vector<VkCommandBuffer> m_commandBuffers;

    // Uniform buffers
    std::vector<std::unique_ptr<vk::VkBufferHandle>> m_uniformBuffers;

    // Default material UBO buffers (binding 2)
    std::vector<std::unique_ptr<vk::VkBufferHandle>> m_materialUboBuffers;
    std::vector<void *> m_materialUboMapped;

    // Lighting UBO buffers (binding 1) - for scene lighting
    std::vector<std::unique_ptr<vk::VkBufferHandle>> m_lightingUboBuffers;
    std::vector<void *> m_lightingUboMapped;

    // Scene light collector
    SceneLightCollector m_lightCollector;

    // Shaders cache
    std::unordered_map<std::string, VkShaderModule> m_vertShaders;
    std::unordered_map<std::string, VkShaderModule> m_fragShaders;

    // Shader code cache for reflection-based pipelines
    std::unordered_map<std::string, std::vector<char>> m_vertShaderCodes;
    std::unordered_map<std::string, std::vector<char>> m_fragShaderCodes;

    // Reflection-based material pipeline manager
    MaterialPipelineManager m_materialPipelineManager;

    // Shader program cache — owned here, injected into MaterialPipelineManager
    ShaderProgramCache m_shaderProgramCache;

    // Textures
    std::unordered_map<std::string, std::unique_ptr<vk::VkTexture>> m_textures;

    // ========================================================================
    // Per-Object GPU Buffers (Phase 2.3.4)
    // ========================================================================

    /// @brief Per-object vertex/index buffer pair with size tracking
    struct PerObjectBuffers
    {
        std::unique_ptr<vk::VkBufferHandle> vertexBuffer;
        std::unique_ptr<vk::VkBufferHandle> indexBuffer;
        size_t vertexCount = 0;
        size_t indexCount = 0;
    };

    /// @brief Map from objectId → persistent GPU buffers.
    /// Buffers are only recreated when vertex/index count changes.
    std::unordered_map<uint64_t, PerObjectBuffers> m_perObjectBuffers;

    // Render callbacks (RenderGraph-based)
    std::function<void(VkCommandBuffer cmdBuf)> m_renderGraphExecutor;
    std::function<void(vk::RenderContext &ctx)> m_guiRenderCallback;

    // Unity-style draw calls for multi-material rendering
    std::vector<DrawCall> m_drawCalls;

    // Pre/Post scene render callbacks
    PostSceneRenderCallback m_postSceneRenderCallback;

    // ========================================================================
    // Shadow Pipeline (lazy-initialized by DrawShadowCasters)
    // ========================================================================
    VkPipeline m_shadowPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_shadowPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_shadowDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_shadowDescPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_shadowDescSets;
    std::vector<VkBuffer> m_shadowUboBuffers;
    std::vector<VmaAllocation> m_shadowUboAllocations;
    std::vector<void *> m_shadowUboMappedPtrs;
    VkSampler m_shadowDepthSampler = VK_NULL_HANDLE;
    VkRenderPass m_shadowCompatRenderPass = VK_NULL_HANDLE; ///< For pipeline compatibility
    bool m_shadowPipelineReady = false;

    /// @brief Lazily create/recreate shadow pipeline resources.
    bool EnsureShadowPipeline(VkRenderPass compatibleRenderPass);
    /// @brief Create shadow depth sampler for shadow map sampling.
    bool CreateShadowDepthSampler();
    /// @brief Cleanup shadow pipeline resources.
    void CleanupShadowPipeline();

    // ========================================================================
    // Per-View Descriptor Set (set 1) — multi-camera shadow isolation
    // ========================================================================
    VkDescriptorSetLayout m_perViewDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_perViewDescPool = VK_NULL_HANDLE;
    VkDescriptorSet m_activeShadowDescSet = VK_NULL_HANDLE; ///< Currently active per-view desc for draw calls

    /// @brief Create per-view descriptor set layout and pool.
    bool CreatePerViewDescriptorResources();
    /// @brief Destroy per-view descriptor set layout and pool.
    void DestroyPerViewDescriptorResources();

    // ========================================================================
    // Frame-safe deferred deletion queue
    // ========================================================================
    FrameDeletionQueue m_deletionQueue;

    // ========================================================================
    // Staged UBO data (CPU-side cache → GPU via vkCmdUpdateBuffer)
    // ========================================================================
    ShaderLightingUBO m_stagedLightingUBO{};
    bool m_lightingUBODirty = false;

    UniformBufferObject m_stagedUBO{};
    bool m_uboDirty = false;
};

} // namespace infengine
