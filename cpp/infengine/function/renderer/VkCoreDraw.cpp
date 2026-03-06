/**
 * @file VkCoreDraw.cpp
 * @brief InfVkCoreModular — Drawing and per-object buffer management
 *
 * Split from InfVkCoreModular.cpp for maintainability.
 * Contains: DrawFrame, DrawSceneMultiMaterial, DrawSceneFiltered,
 *           SetDrawCalls, EnsureObjectBuffers, CleanupUnusedBuffers.
 */

#include "InfError.h"
#include "InfVkCoreModular.h"
#include "SceneRenderGraph.h"
#include "vk/VkTypes.h"

#include <function/renderer/shader/ShaderProgram.h>
#include <function/resources/InfMaterial/InfMaterial.h>
#include <function/scene/LightingData.h>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <unordered_set>

namespace infengine
{

// ============================================================================
// Rendering
// ============================================================================

void InfVkCoreModular::DrawFrame(const float *viewPos, const float *viewLookAt, const float *viewUp)
{
    // Skip rendering when the window is minimized (zero extent).
    // Without this guard, vkAcquireNextImageKHR blocks indefinitely
    // because the swapchain has no presentable images at 0×0.
    {
        VkExtent2D ext = m_swapchain.GetExtent();
        if (ext.width == 0 || ext.height == 0) {
            // Yield a bit so we don't spin-lock the CPU while minimized
            SDL_Delay(16);
            return;
        }
    }

    // Acquire next swapchain image
    uint32_t imageIndex;
    auto result = m_swapchain.AcquireNextImage(imageIndex);

    if (result == vk::SwapchainResult::NeedRecreate) {
        RecreateSwapchain();
        return;
    }

    if (result == vk::SwapchainResult::Error) {
        INFLOG_ERROR("Failed to acquire swapchain image");
        return;
    }

    // Update uniform buffer
    UpdateUniformBuffer(m_currentFrame, viewPos, viewLookAt, viewUp);

    // Reset fence for this frame before submission
    m_swapchain.ResetCurrentFence();

    // Reset and record command buffer
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    RecordCommandBuffer(imageIndex);

    // Submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {m_swapchain.GetImageAvailableSemaphore()};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffers[m_currentFrame];

    VkSemaphore signalSemaphores[] = {m_swapchain.GetRenderFinishedSemaphore()};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult submitResult =
        vkQueueSubmit(m_deviceContext.GetGraphicsQueue(), 1, &submitInfo, m_swapchain.GetInFlightFence());
    if (submitResult != VK_SUCCESS) {
        INFLOG_ERROR("Failed to submit draw command buffer: ", vk::VkResultToString(submitResult));
    }

    // Present
    result = m_swapchain.Present(imageIndex);
    if (result == vk::SwapchainResult::NeedRecreate || m_framebufferResized) {
        m_framebufferResized = false;
        RecreateSwapchain();
    }

    // Advance frame
    m_swapchain.AdvanceFrame();
    m_currentFrame = (m_currentFrame + 1) % m_maxFramesInFlight;
}

void InfVkCoreModular::DrawSceneMultiMaterial(VkCommandBuffer cmdBuf, uint32_t width, uint32_t height)
{
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width, height};
    vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

    // Per-object VBO path (Phase 2.3.4): each draw call binds its own buffer
    bool hasAnyBuffers = !m_perObjectBuffers.empty();
    if (!hasAnyBuffers) {
        return;
    }

    VkPipeline currentPipeline = VK_NULL_HANDLE;
    VkPipelineLayout currentLayout = VK_NULL_HANDLE;
    VkDescriptorSet currentDescriptorSet = VK_NULL_HANDLE;
    std::shared_ptr<InfMaterial> currentMaterial = nullptr;
    VkBuffer currentVertexBuffer = VK_NULL_HANDLE; // Track to minimize rebinds

    auto defaultMaterial = MaterialManager::Instance().GetDefaultMaterial();
    auto errorMaterial = MaterialManager::Instance().GetErrorMaterial();

    static int drawSceneLog = 0;

    for (const DrawCall &dc : m_drawCalls) {
        // Skip objects that were frustum-culled by the main camera.
        // Shadow passes use DrawShadowCasters which does NOT check this flag.
        if (!dc.frustumVisible)
            continue;

        auto material = dc.material ? dc.material : defaultMaterial;
        if (!material) {
            continue;
        }

        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

        // ---- Single-pass pipeline selection ----
        pipeline = material->GetPipeline();
        pipelineLayout = material->GetPipelineLayout();

        // If the material's shader config changed (e.g. user switched shaders in the
        // Inspector), the cached pipeline handle is stale. Force re-evaluation so the
        // fallback chain can kick in and the error material is used when needed.
        if (pipeline != VK_NULL_HANDLE && material->IsPipelineDirty()) {
            pipeline = VK_NULL_HANDLE;
            pipelineLayout = VK_NULL_HANDLE;
        }

        if (pipeline == VK_NULL_HANDLE) {
            const std::string &vertPath = material->GetVertexShaderPath();
            const std::string &fragPath = material->GetFragmentShaderPath();

            if (drawSceneLog < 5) {
                INFLOG_INFO("DrawSceneMultiMaterial: material '", material->GetName(), "' has no pipeline, vert='",
                            vertPath, "', frag='", fragPath, "'");
            }

            if (!vertPath.empty() && !fragPath.empty()) {
                if (RefreshMaterialPipeline(material, vertPath, fragPath)) {
                    pipeline = material->GetPipeline();
                    pipelineLayout = material->GetPipelineLayout();
                    if (drawSceneLog < 5) {
                        INFLOG_INFO("DrawSceneMultiMaterial: pipeline created for '", material->GetName(), "'");
                    }
                }
            }

            // Fall back to error material (magenta unlit) on pipeline failure
            if (pipeline == VK_NULL_HANDLE && errorMaterial) {
                // Lazily build error material pipeline if not yet created
                if (errorMaterial->GetPipeline() == VK_NULL_HANDLE) {
                    const std::string &errVert = errorMaterial->GetVertexShaderPath();
                    const std::string &errFrag = errorMaterial->GetFragmentShaderPath();
                    if (!errVert.empty() && !errFrag.empty()) {
                        RefreshMaterialPipeline(errorMaterial, errVert, errFrag);
                    }
                }
                if (errorMaterial->GetPipeline() != VK_NULL_HANDLE) {
                    if (drawSceneLog < 5) {
                        INFLOG_WARN("DrawSceneMultiMaterial: falling back to error material for '", material->GetName(),
                                    "'");
                    }
                    pipeline = errorMaterial->GetPipeline();
                    pipelineLayout = errorMaterial->GetPipelineLayout();
                    material = errorMaterial;
                }
            }
            if (pipeline == VK_NULL_HANDLE && defaultMaterial) {
                if (drawSceneLog < 5) {
                    INFLOG_WARN("DrawSceneMultiMaterial: falling back to default for '", material->GetName(), "'");
                }
                pipeline = defaultMaterial->GetPipeline();
                pipelineLayout = defaultMaterial->GetPipelineLayout();
                material = defaultMaterial;
            }

            if (pipeline == VK_NULL_HANDLE) {
                continue;
            }
            drawSceneLog++;
        }

        descriptorSet = material->GetDescriptorSet();

        if (pipeline != currentPipeline) {
            vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            currentPipeline = pipeline;
        }

        if (material != currentMaterial) {
            UpdateMaterialUBO(*material);
            currentMaterial = material;
            currentLayout = VK_NULL_HANDLE;
            currentDescriptorSet = VK_NULL_HANDLE;
        }

        VkPipelineLayout layout = pipelineLayout;

        if (descriptorSet == VK_NULL_HANDLE) {
            INFLOG_WARN("DrawSceneMultiMaterial: descriptor set not ready for material '", material->GetName(), "'");
            continue;
        }

        if (descriptorSet != currentDescriptorSet || layout != currentLayout) {
            vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &descriptorSet, 0, nullptr);
            currentDescriptorSet = descriptorSet;
            currentLayout = layout;

            // Bind per-view shadow descriptor (set 1) for lit shaders
            if (m_activeShadowDescSet != VK_NULL_HANDLE) {
                ShaderProgram *program = material->GetShaderProgram();
                if (program && program->GetDescriptorSetLayout(1) != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1,
                                            &m_activeShadowDescSet, 0, nullptr);
                }
            }
        }

        // Push per-object model matrix + normal matrix via push constants
        // The normal matrix is transpose(inverse(mat3(model))), precomputed on CPU
        // to avoid expensive per-vertex inverse() in the shader (Phase 4 optimization).
        struct PushConstants
        {
            glm::mat4 model;
            glm::mat4 normalMat; // only upper-left 3x3 used by shader
        };

        PushConstants pushData;
        pushData.model = dc.worldMatrix;
        // Compute normal matrix: transpose(inverse(mat3(model))) packed into mat4
        glm::mat3 normalMat3 = glm::transpose(glm::inverse(glm::mat3(dc.worldMatrix)));
        pushData.normalMat = glm::mat4(normalMat3); // extends mat3→mat4 with 0s and 1 in [3][3]

        vkCmdPushConstants(cmdBuf, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pushData);

        // ---- Per-object VBO binding (Phase 2.3.4) ----
        // Each draw call binds its own VBO/IBO from the per-object buffer cache.
        // Falls back to legacy combined buffer if no per-object buffer exists.
        auto bufIt = m_perObjectBuffers.find(dc.objectId);
        if (bufIt != m_perObjectBuffers.end() && bufIt->second.vertexBuffer && bufIt->second.indexBuffer) {
            VkBuffer vb = bufIt->second.vertexBuffer->GetBuffer();
            if (vb != currentVertexBuffer) {
                VkBuffer vertBuffers[] = {vb};
                VkDeviceSize vbOffsets[] = {0};
                vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertBuffers, vbOffsets);
                vkCmdBindIndexBuffer(cmdBuf, bufIt->second.indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
                currentVertexBuffer = vb;
            }
            vkCmdDrawIndexed(cmdBuf, dc.indexCount, 1, 0, 0, 0); // indexStart always 0 for per-object
        }
    }
}

void InfVkCoreModular::SetDrawCalls(const std::vector<DrawCall> &drawCalls)
{
    m_drawCalls = drawCalls;

    // NOTE: We no longer re-sort draw calls here. The ordering is
    // determined by ScriptableRenderContext::DrawRenderers() — it filters by
    // render queue range and appends in the order specified by the pipeline.
    // Re-sorting here would override SRC's multi-pass ordering.
}

// ============================================================================
// Multi-camera UBO update via command buffer
// ============================================================================

void InfVkCoreModular::CmdUpdateUniformBuffer(VkCommandBuffer cmdBuf, const glm::mat4 &view, const glm::mat4 &proj)
{
    // All material descriptor sets reference m_uniformBuffers[0] (hardcoded
    // in MaterialDescriptorManager). Always target buffer 0 regardless of
    // m_currentFrame so the shaders see the updated VP matrices.
    if (m_uniformBuffers.empty() || !m_uniformBuffers[0]) {
        return;
    }

    VkBuffer buffer = m_uniformBuffers[0]->GetBuffer();

    UniformBufferObject ubo{};
    ubo.model = glm::mat4(1.0f);
    ubo.view = view;
    ubo.proj = proj;

    // Barrier: ensure previous shader reads from the UBO are complete
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    // Update the UBO inline in the command buffer
    vkCmdUpdateBuffer(cmdBuf, buffer, 0, sizeof(ubo), &ubo);

    // Barrier: ensure write is visible before subsequent shader reads
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier, 0,
                         nullptr, 0, nullptr);
}

void InfVkCoreModular::CmdUpdateShadowUBO(VkCommandBuffer cmdBuf)
{
    if (m_shadowUboBuffers.empty()) {
        if (!EnsureShadowPipeline(VK_NULL_HANDLE)) {
            return;
        }
    }

    const uint32_t frameIndex = m_currentFrame % m_maxFramesInFlight;
    if (frameIndex >= m_shadowUboBuffers.size() || frameIndex >= m_shadowDescSets.size()) {
        return;
    }

    struct ShadowUBO
    {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
    };

    ShadowUBO shadowUbo{};
    shadowUbo.model = glm::mat4(1.0f);
    shadowUbo.view = glm::mat4(1.0f);
    shadowUbo.proj = m_lightCollector.GetShadowLightVP();

    VkBuffer shadowBuffer = m_shadowUboBuffers[frameIndex];
    if (shadowBuffer == VK_NULL_HANDLE) {
        return;
    }

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    vkCmdUpdateBuffer(cmdBuf, shadowBuffer, 0, sizeof(ShadowUBO), &shadowUbo);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier, 0,
                         nullptr, 0, nullptr);
}

// ============================================================================
// Phase 2: Filtered Draw — renders only draw calls within a queue range
// ============================================================================

void InfVkCoreModular::DrawSceneFiltered(VkCommandBuffer cmdBuf, uint32_t width, uint32_t height, int queueMin,
                                         int queueMax)
{
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width, height};
    vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

    bool hasAnyBuffers = !m_perObjectBuffers.empty();
    if (!hasAnyBuffers) {
        return;
    }

    VkPipeline currentPipeline = VK_NULL_HANDLE;
    VkPipelineLayout currentLayout = VK_NULL_HANDLE;
    VkDescriptorSet currentDescriptorSet = VK_NULL_HANDLE;
    std::shared_ptr<InfMaterial> currentMaterial = nullptr;
    VkBuffer currentVertexBuffer = VK_NULL_HANDLE;

    auto defaultMaterial = MaterialManager::Instance().GetDefaultMaterial();
    auto errorMaterial = MaterialManager::Instance().GetErrorMaterial();

    for (const DrawCall &dc : m_drawCalls) {
        // Skip frustum-culled objects in the main camera pass.
        // Shadow caster pass (DrawShadowCasters) does NOT check this.
        if (!dc.frustumVisible)
            continue;

        auto material = dc.material ? dc.material : defaultMaterial;
        if (!material) {
            continue;
        }

        // Phase 2: Filter by render queue range
        int queue = material->GetRenderQueue();
        if (queue < queueMin || queue > queueMax) {
            continue;
        }

        VkPipeline pipeline = material->GetPipeline();
        VkPipelineLayout pipelineLayout = material->GetPipelineLayout();

        // Force re-evaluation when material's shader config changed
        if (pipeline != VK_NULL_HANDLE && material->IsPipelineDirty()) {
            pipeline = VK_NULL_HANDLE;
            pipelineLayout = VK_NULL_HANDLE;
        }

        if (pipeline == VK_NULL_HANDLE) {
            const std::string &vertPath = material->GetVertexShaderPath();
            const std::string &fragPath = material->GetFragmentShaderPath();
            if (!vertPath.empty() && !fragPath.empty()) {
                RefreshMaterialPipeline(material, vertPath, fragPath);
                pipeline = material->GetPipeline();
                pipelineLayout = material->GetPipelineLayout();
            }
            // Fall back to error material (magenta unlit) on pipeline failure
            if (pipeline == VK_NULL_HANDLE && errorMaterial) {
                // Lazily build error material pipeline if not yet created
                if (errorMaterial->GetPipeline() == VK_NULL_HANDLE) {
                    const std::string &errVert = errorMaterial->GetVertexShaderPath();
                    const std::string &errFrag = errorMaterial->GetFragmentShaderPath();
                    if (!errVert.empty() && !errFrag.empty()) {
                        RefreshMaterialPipeline(errorMaterial, errVert, errFrag);
                    }
                }
                if (errorMaterial->GetPipeline() != VK_NULL_HANDLE) {
                    pipeline = errorMaterial->GetPipeline();
                    pipelineLayout = errorMaterial->GetPipelineLayout();
                    material = errorMaterial;
                }
            }
            if (pipeline == VK_NULL_HANDLE && defaultMaterial) {
                pipeline = defaultMaterial->GetPipeline();
                pipelineLayout = defaultMaterial->GetPipelineLayout();
                material = defaultMaterial;
            }
            if (pipeline == VK_NULL_HANDLE) {
                continue;
            }
        }

        VkDescriptorSet descriptorSet = material->GetDescriptorSet();

        if (pipeline != currentPipeline) {
            vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            currentPipeline = pipeline;
        }

        if (material != currentMaterial) {
            UpdateMaterialUBO(*material);
            currentMaterial = material;
            currentLayout = VK_NULL_HANDLE;
            currentDescriptorSet = VK_NULL_HANDLE;
        }

        if (descriptorSet == VK_NULL_HANDLE) {
            continue;
        }

        if (descriptorSet != currentDescriptorSet || pipelineLayout != currentLayout) {
            vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0,
                                    nullptr);
            currentDescriptorSet = descriptorSet;
            currentLayout = pipelineLayout;

            // Bind per-view descriptor set (set 1) for lit shaders that have
            // a shadow map sampler at set=1. This descriptor is per-render-graph,
            // enabling multi-camera shadow isolation.
            if (m_activeShadowDescSet != VK_NULL_HANDLE) {
                ShaderProgram *program = material->GetShaderProgram();
                if (program && program->GetDescriptorSetLayout(1) != VK_NULL_HANDLE) {
                    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1,
                                            &m_activeShadowDescSet, 0, nullptr);
                }
            }
        }

        struct PushConstants
        {
            glm::mat4 model;
            glm::mat4 normalMat;
        };

        PushConstants pushData;
        pushData.model = dc.worldMatrix;
        glm::mat3 normalMat3 = glm::transpose(glm::inverse(glm::mat3(dc.worldMatrix)));
        pushData.normalMat = glm::mat4(normalMat3);

        vkCmdPushConstants(cmdBuf, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pushData);

        auto bufIt = m_perObjectBuffers.find(dc.objectId);
        if (bufIt != m_perObjectBuffers.end() && bufIt->second.vertexBuffer && bufIt->second.indexBuffer) {
            VkBuffer vb = bufIt->second.vertexBuffer->GetBuffer();
            if (vb != currentVertexBuffer) {
                VkBuffer vertBuffers[] = {vb};
                VkDeviceSize vbOffsets[] = {0};
                vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertBuffers, vbOffsets);
                vkCmdBindIndexBuffer(cmdBuf, bufIt->second.indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
                currentVertexBuffer = vb;
            }
            vkCmdDrawIndexed(cmdBuf, dc.indexCount, 1, 0, 0, 0);
        }
    }
}

// ============================================================================
// Shadow Caster Draw — renders shadow-casting objects with shadow pipeline
// ============================================================================

void InfVkCoreModular::DrawShadowCasters(VkCommandBuffer cmdBuf, uint32_t width, uint32_t height, int queueMin,
                                         int queueMax)
{
    // Skip if shadow shaders not loaded yet
    if (!HasShader("shadow", "vertex") || !HasShader("shadow", "fragment")) {
        return;
    }

    // Ensure shadow pipeline is ready (lazy init)
    // We pass VK_NULL_HANDLE — the pipeline will be created with a compatible
    // depth-only render pass that we manage ourselves.
    if (!m_shadowPipelineReady) {
        if (!EnsureShadowPipeline(VK_NULL_HANDLE)) {
            return;
        }
    }

    // Set viewport and scissor to shadow map dimensions
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(width);
    viewport.height = static_cast<float>(height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {width, height};
    vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

    const uint32_t frameIndex = m_currentFrame % m_maxFramesInFlight;
    if (frameIndex >= m_shadowUboBuffers.size() || frameIndex >= m_shadowDescSets.size()) {
        return;
    }
    VkDescriptorSet currentShadowDescSet = m_shadowDescSets[frameIndex];

    // Bind shadow pipeline
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipeline);

    // Bind shadow descriptor set (binding 0 = light VP UBO)
    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipelineLayout, 0, 1,
                            &currentShadowDescSet, 0, nullptr);

    // Draw all objects in queue range
    VkBuffer currentVertexBuffer = VK_NULL_HANDLE;

    for (const DrawCall &dc : m_drawCalls) {
        if (!dc.material)
            continue;

        int renderQueue = dc.material->GetRenderQueue();
        if (renderQueue < queueMin || renderQueue > queueMax)
            continue;

        auto bufIt = m_perObjectBuffers.find(dc.objectId);
        if (bufIt == m_perObjectBuffers.end() || !bufIt->second.vertexBuffer || !bufIt->second.indexBuffer)
            continue;

        // Push model matrix + normal matrix (128 bytes)
        struct PushData
        {
            glm::mat4 model;
            glm::mat4 normalMat;
        } pushData;
        pushData.model = dc.worldMatrix;
        pushData.normalMat = glm::transpose(glm::inverse(dc.worldMatrix));

        vkCmdPushConstants(cmdBuf, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushData), &pushData);

        VkBuffer vb = bufIt->second.vertexBuffer->GetBuffer();
        if (vb != currentVertexBuffer) {
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vb, offsets);
            vkCmdBindIndexBuffer(cmdBuf, bufIt->second.indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
            currentVertexBuffer = vb;
        }

        vkCmdDrawIndexed(cmdBuf, dc.indexCount, 1, dc.indexStart, 0, 0);
    }
}

// ============================================================================
// Shadow Pipeline Management
// ============================================================================

bool InfVkCoreModular::EnsureShadowPipeline(VkRenderPass /*compatibleRenderPass*/)
{
    if (m_shadowPipelineReady)
        return true;

    VkDevice device = GetDevice();

    // --- Create a compatible depth-only render pass ---
    if (m_shadowCompatRenderPass == VK_NULL_HANDLE) {
        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = VK_FORMAT_D32_SFLOAT;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 0;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 0;
        subpass.pDepthStencilAttachment = &depthRef;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &depthAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;

        if (vkCreateRenderPass(device, &rpInfo, nullptr, &m_shadowCompatRenderPass) != VK_SUCCESS) {
            INFLOG_ERROR("Failed to create shadow-compatible render pass");
            return false;
        }
    }

    // --- Create descriptor set layout (binding 0 = UBO) ---
    if (m_shadowDescSetLayout == VK_NULL_HANDLE) {
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboBinding;

        if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_shadowDescSetLayout) != VK_SUCCESS) {
            INFLOG_ERROR("Failed to create shadow descriptor set layout");
            return false;
        }
    }

    // --- Create descriptor pool ---
    if (m_shadowDescPool == VK_NULL_HANDLE) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = m_maxFramesInFlight;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = m_maxFramesInFlight;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_shadowDescPool) != VK_SUCCESS) {
            INFLOG_ERROR("Failed to create shadow descriptor pool");
            return false;
        }
    }

    // --- Allocate descriptor set ---
    if (m_shadowDescSets.empty()) {
        m_shadowDescSets.resize(m_maxFramesInFlight, VK_NULL_HANDLE);
        std::vector<VkDescriptorSetLayout> setLayouts(m_maxFramesInFlight, m_shadowDescSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_shadowDescPool;
        allocInfo.descriptorSetCount = m_maxFramesInFlight;
        allocInfo.pSetLayouts = setLayouts.data();

        if (vkAllocateDescriptorSets(device, &allocInfo, m_shadowDescSets.data()) != VK_SUCCESS) {
            INFLOG_ERROR("Failed to allocate shadow descriptor set");
            return false;
        }
    }

    // --- Create per-frame UBO buffers ---
    if (m_shadowUboBuffers.empty()) {
        // UBO contains: model(mat4) + view(mat4) + proj(mat4) = 192 bytes
        VkDeviceSize uboSize = sizeof(glm::mat4) * 3;

        m_shadowUboBuffers.resize(m_maxFramesInFlight, VK_NULL_HANDLE);
        m_shadowUboAllocations.resize(m_maxFramesInFlight, VK_NULL_HANDLE);
        m_shadowUboMappedPtrs.resize(m_maxFramesInFlight, nullptr);

        for (uint32_t frame = 0; frame < m_maxFramesInFlight; ++frame) {
            VkBufferCreateInfo bufInfo{};
            bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufInfo.size = uboSize;
            bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocator allocator = m_deviceContext.GetVmaAllocator();
            VmaAllocationCreateInfo allocCreateInfo{};
            allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            VmaAllocationInfo vmaAllocInfo{};
            VkResult result = vmaCreateBuffer(allocator, &bufInfo, &allocCreateInfo, &m_shadowUboBuffers[frame],
                                              &m_shadowUboAllocations[frame], &vmaAllocInfo);
            if (result != VK_SUCCESS) {
                INFLOG_ERROR("Failed to create shadow UBO buffer via VMA");
                return false;
            }
            m_shadowUboMappedPtrs[frame] = vmaAllocInfo.pMappedData;

            VkDescriptorBufferInfo bufDesc{};
            bufDesc.buffer = m_shadowUboBuffers[frame];
            bufDesc.offset = 0;
            bufDesc.range = uboSize;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = m_shadowDescSets[frame];
            write.dstBinding = 0;
            write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.descriptorCount = 1;
            write.pBufferInfo = &bufDesc;

            vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
        }
    }

    // --- Create shadow depth sampler ---
    if (m_shadowDepthSampler == VK_NULL_HANDLE) {
        if (!CreateShadowDepthSampler()) {
            return false;
        }
    }

    // --- Create shadow pipeline ---
    VkShaderModule vertModule = GetShaderModule("shadow", "vertex");
    VkShaderModule fragModule = GetShaderModule("shadow", "fragment");

    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        INFLOG_WARN("Shadow shader modules not available yet");
        return false;
    }

    // Shader stages
    VkPipelineShaderStageCreateInfo vertStage{};
    vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStage.module = vertModule;
    vertStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragStage{};
    fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStage.module = fragModule;
    fragStage.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertStage, fragStage};

    // Vertex input (same as scene rendering)
    auto bindingDesc = Vertex::getBindingDescription();
    auto attrDescs = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterization — front-face culling + depth bias for shadow acne
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.5f;
    rasterizer.depthBiasSlopeFactor = 1.0f;
    rasterizer.depthBiasClamp = 0.01f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 0;

    std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(glm::mat4) * 2; // model + normalMat

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_shadowDescSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_shadowPipelineLayout) != VK_SUCCESS) {
        INFLOG_ERROR("Failed to create shadow pipeline layout");
        return false;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_shadowPipelineLayout;
    pipelineInfo.renderPass = m_shadowCompatRenderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_shadowPipeline) != VK_SUCCESS) {
        INFLOG_ERROR("Failed to create shadow pipeline");
        return false;
    }

    m_shadowPipelineReady = true;
    INFLOG_INFO("Shadow pipeline created successfully");
    return true;
}

bool InfVkCoreModular::CreateShadowDepthSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
    samplerInfo.maxLod = 1.0f;

    if (vkCreateSampler(GetDevice(), &samplerInfo, nullptr, &m_shadowDepthSampler) != VK_SUCCESS) {
        INFLOG_ERROR("Failed to create shadow depth sampler");
        return false;
    }
    return true;
}

void InfVkCoreModular::CleanupShadowPipeline()
{
    VkDevice device = GetDevice();
    if (device == VK_NULL_HANDLE)
        return;

    if (m_shadowPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_shadowPipeline, nullptr);
        m_shadowPipeline = VK_NULL_HANDLE;
    }
    if (m_shadowPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_shadowPipelineLayout, nullptr);
        m_shadowPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_shadowDescPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_shadowDescPool, nullptr);
        m_shadowDescPool = VK_NULL_HANDLE;
        m_shadowDescSets.clear();
    }
    if (m_shadowDescSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_shadowDescSetLayout, nullptr);
        m_shadowDescSetLayout = VK_NULL_HANDLE;
    }
    if (!m_shadowUboBuffers.empty()) {
        VmaAllocator allocator = m_deviceContext.GetVmaAllocator();
        for (size_t i = 0; i < m_shadowUboBuffers.size(); ++i) {
            if (m_shadowUboBuffers[i] != VK_NULL_HANDLE) {
                vmaDestroyBuffer(allocator, m_shadowUboBuffers[i], m_shadowUboAllocations[i]);
            }
        }
        m_shadowUboBuffers.clear();
        m_shadowUboAllocations.clear();
        m_shadowUboMappedPtrs.clear();
    }
    if (m_shadowDepthSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_shadowDepthSampler, nullptr);
        m_shadowDepthSampler = VK_NULL_HANDLE;
    }
    if (m_shadowCompatRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_shadowCompatRenderPass, nullptr);
        m_shadowCompatRenderPass = VK_NULL_HANDLE;
    }
    m_shadowPipelineReady = false;
}

// ============================================================================
// Per-Object Buffer Management (Phase 2.3.4)
// ============================================================================

void InfVkCoreModular::EnsureObjectBuffers(uint64_t objectId, const std::vector<Vertex> &vertices,
                                           const std::vector<uint32_t> &indices, bool forceUpdate)
{
    if (vertices.empty() || indices.empty())
        return;

    auto it = m_perObjectBuffers.find(objectId);
    if (it != m_perObjectBuffers.end() && !forceUpdate) {
        // Buffer already exists — only recreate if size changed
        if (it->second.vertexCount == vertices.size() && it->second.indexCount == indices.size()) {
            return; // Same size, no update needed (static mesh)
        }
    }

    // Create or recreate buffers
    PerObjectBuffers buffers;
    buffers.vertexBuffer = m_resourceManager.CreateVertexBuffer(vertices.data(), vertices.size() * sizeof(Vertex));
    buffers.indexBuffer = m_resourceManager.CreateIndexBuffer(indices.data(), indices.size() * sizeof(uint32_t));
    buffers.vertexCount = vertices.size();
    buffers.indexCount = indices.size();

    m_perObjectBuffers[objectId] = std::move(buffers);
}

void InfVkCoreModular::CleanupUnusedBuffers(const std::vector<DrawCall> &activeDrawCalls)
{
    // Build set of active objectIds
    std::unordered_set<uint64_t> activeIds;
    for (const auto &dc : activeDrawCalls) {
        activeIds.insert(dc.objectId);
    }

    // Remove buffers for objects no longer in the scene.
    // Actual GPU resource destruction is deferred via FrameDeletionQueue
    // so that in-flight command buffers are never invalidated.
    for (auto it = m_perObjectBuffers.begin(); it != m_perObjectBuffers.end();) {
        if (activeIds.find(it->first) == activeIds.end()) {
            // Move the buffer ownership into the deletion queue
            auto buffers = std::make_shared<PerObjectBuffers>(std::move(it->second));
            m_deletionQueue.Push([buffers]() mutable {
                buffers->vertexBuffer.reset();
                buffers->indexBuffer.reset();
            });
            it = m_perObjectBuffers.erase(it);
        } else {
            ++it;
        }
    }
}

// ============================================================================
// Per-View Descriptor Set (set 1) — multi-camera shadow isolation
// ============================================================================

bool InfVkCoreModular::CreatePerViewDescriptorResources()
{
    VkDevice device = GetDevice();
    if (device == VK_NULL_HANDLE)
        return false;

    // Layout: set 1, binding 0 = combined image sampler (shadow map), fragment stage
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = nullptr;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_perViewDescSetLayout) != VK_SUCCESS) {
        INFLOG_ERROR("Failed to create per-view descriptor set layout");
        return false;
    }

    // Pool: enough for multiple render graphs (scene + game + future cameras)
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 16; // Up to 16 render graphs

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 16;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_perViewDescPool) != VK_SUCCESS) {
        INFLOG_ERROR("Failed to create per-view descriptor pool");
        vkDestroyDescriptorSetLayout(device, m_perViewDescSetLayout, nullptr);
        m_perViewDescSetLayout = VK_NULL_HANDLE;
        return false;
    }

    INFLOG_INFO("Created per-view descriptor set layout and pool (multi-camera shadow)");
    return true;
}

void InfVkCoreModular::DestroyPerViewDescriptorResources()
{
    VkDevice device = GetDevice();
    if (device == VK_NULL_HANDLE)
        return;

    m_activeShadowDescSet = VK_NULL_HANDLE;

    if (m_perViewDescPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_perViewDescPool, nullptr);
        m_perViewDescPool = VK_NULL_HANDLE;
    }
    if (m_perViewDescSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_perViewDescSetLayout, nullptr);
        m_perViewDescSetLayout = VK_NULL_HANDLE;
    }
}

VkDescriptorSet InfVkCoreModular::AllocatePerViewDescriptorSet()
{
    if (m_perViewDescSetLayout == VK_NULL_HANDLE || m_perViewDescPool == VK_NULL_HANDLE) {
        INFLOG_ERROR("Per-view descriptor resources not initialized");
        return VK_NULL_HANDLE;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_perViewDescPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_perViewDescSetLayout;

    VkDescriptorSet descSet = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(GetDevice(), &allocInfo, &descSet) != VK_SUCCESS) {
        INFLOG_ERROR("Failed to allocate per-view descriptor set");
        return VK_NULL_HANDLE;
    }

    // Initialize with default (white) texture so shaders don't sample garbage
    ClearPerViewShadowMap(descSet);

    return descSet;
}

void InfVkCoreModular::UpdatePerViewShadowMap(VkDescriptorSet perViewDescSet, VkImageView shadowView,
                                              VkSampler shadowSampler)
{
    if (perViewDescSet == VK_NULL_HANDLE || shadowView == VK_NULL_HANDLE || shadowSampler == VK_NULL_HANDLE)
        return;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = shadowView;
    imageInfo.sampler = shadowSampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = perViewDescSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(GetDevice(), 1, &write, 0, nullptr);
}

void InfVkCoreModular::ClearPerViewShadowMap(VkDescriptorSet perViewDescSet)
{
    if (perViewDescSet == VK_NULL_HANDLE)
        return;

    // Use default white texture so depth comparison = 1.0 → fully lit (no shadow)
    auto &descMgr = m_materialPipelineManager.GetDescriptorManager();
    VkImageView defaultView = descMgr.GetDefaultImageView();
    VkSampler defaultSampler = descMgr.GetDefaultSampler();

    if (defaultView == VK_NULL_HANDLE || defaultSampler == VK_NULL_HANDLE) {
        INFLOG_WARN("ClearPerViewShadowMap: default texture not available");
        return;
    }

    UpdatePerViewShadowMap(perViewDescSet, defaultView, defaultSampler);
}

} // namespace infengine
