/**
 * @file VkCoreDraw.cpp
 * @brief InfVkCoreModular — Drawing and per-object buffer management
 *
 * Split from InfVkCoreModular.cpp for maintainability.
 * Contains: DrawFrame, DrawSceneFiltered,
 *           SetDrawCalls, EnsureObjectBuffers, CleanupUnusedBuffers.
 */

#include "InfError.h"
#include "InfVkCoreModular.h"
#include "ProfileConfig.h"
#include "SceneRenderGraph.h"
#include "vk/VkTypes.h"

#include <function/renderer/Frustum.h>
#include <function/renderer/shader/ShaderProgram.h>
#include <function/resources/AssetRegistry/AssetRegistry.h>
#include <function/resources/InfMaterial/InfMaterial.h>
#include <function/scene/LightingData.h>

#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <unordered_set>

namespace infengine
{

// ============================================================================
// Rendering
// ============================================================================

void InfVkCoreModular::DrawFrame(const float *viewPos, const float *viewLookAt, const float *viewUp)
{
#if INFENGINE_FRAME_PROFILE
    using Clock = std::chrono::high_resolution_clock;
    auto _t0 = Clock::now();
    auto _tPrev = _t0;
    auto _tNow = _t0;
#endif

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
#if INFENGINE_FRAME_PROFILE
    _tNow = Clock::now();
    m_drawSubMs[0] += std::chrono::duration<double, std::milli>(_tNow - _tPrev).count();
    _tPrev = _tNow;
#endif

    // Update uniform buffer
    UpdateUniformBuffer(m_currentFrame, viewPos, viewLookAt, viewUp);

    // Reset fence for this frame before submission
    m_swapchain.ResetCurrentFence();

    // Reset and record command buffer
    vkResetCommandBuffer(m_commandBuffers[m_currentFrame], 0);
    RecordCommandBuffer(imageIndex);
#if INFENGINE_FRAME_PROFILE
    _tNow = Clock::now();
    m_drawSubMs[1] += std::chrono::duration<double, std::milli>(_tNow - _tPrev).count();
    _tPrev = _tNow;
#endif

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

    VkSemaphore signalSemaphores[] = {m_swapchain.GetRenderFinishedSemaphore(imageIndex)};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkResult submitResult =
        vkQueueSubmit(m_deviceContext.GetGraphicsQueue(), 1, &submitInfo, m_swapchain.GetInFlightFence());
    if (submitResult != VK_SUCCESS) {
        INFLOG_ERROR("Failed to submit draw command buffer: ", vk::VkResultToString(submitResult));
    }
#if INFENGINE_FRAME_PROFILE
    _tNow = Clock::now();
    m_drawSubMs[2] += std::chrono::duration<double, std::milli>(_tNow - _tPrev).count();
    _tPrev = _tNow;
#endif

    // Present
    result = m_swapchain.Present(imageIndex);
    if (result == vk::SwapchainResult::NeedRecreate || m_framebufferResized) {
        m_framebufferResized = false;
        RecreateSwapchain();
    }
#if INFENGINE_FRAME_PROFILE
    _tNow = Clock::now();
    m_drawSubMs[3] += std::chrono::duration<double, std::milli>(_tNow - _tPrev).count();

    ++m_drawSubCount;
#endif

    // Advance frame
    m_swapchain.AdvanceFrame();
    m_currentFrame = (m_currentFrame + 1) % m_maxFramesInFlight;
}

void InfVkCoreModular::SetDrawCalls(const std::vector<DrawCall> *drawCalls)
{
    m_drawCallsPtr = drawCalls;

    // Refresh cached builtin materials (avoids string-hash lookup per DrawSceneFiltered call)
    if (!m_cachedDefaultLit) {
        m_cachedDefaultLit = AssetRegistry::Instance().GetBuiltinMaterial("DefaultLit");
        m_cachedErrorMat = AssetRegistry::Instance().GetBuiltinMaterial("ErrorMaterial");
    }
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
    const uint32_t cascadeCount = m_lightCollector.GetShadowCascadeCount();
    if (cascadeCount == 0)
        return;

    struct ShadowUBO
    {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 proj;
    };

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    for (uint32_t ci = 0; ci < cascadeCount; ++ci) {
        uint32_t bufIdx = frameIndex * NUM_SHADOW_CASCADES + ci;
        if (bufIdx >= m_shadowUboBuffers.size())
            break;

        ShadowUBO shadowUbo{};
        shadowUbo.model = glm::mat4(1.0f);
        shadowUbo.view = glm::mat4(1.0f);
        shadowUbo.proj = m_lightCollector.GetShadowLightVP(ci);

        VkBuffer shadowBuffer = m_shadowUboBuffers[bufIdx];
        if (shadowBuffer == VK_NULL_HANDLE)
            continue;

        vkCmdUpdateBuffer(cmdBuf, shadowBuffer, 0, sizeof(ShadowUBO), &shadowUbo);
    }

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
                                         int queueMax, const std::string &sortMode, const std::string &overrideMaterial,
                                         const std::string &passTag)
{
#if INFENGINE_FRAME_PROFILE
    using Clock = std::chrono::high_resolution_clock;
    const auto totalStart = Clock::now();
    auto stageStart = totalStart;
#endif

    // Fast early-out when no draw calls are staged
    if (drawCalls().empty())
        return;

#if INFENGINE_FRAME_PROFILE
    ++m_drawSceneFilteredCalls;
#endif

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

    const auto &defaultMaterial = m_cachedDefaultLit;
    const auto &errorMaterial = m_cachedErrorMat;
    if (!defaultMaterial) {
        return;
    }

    // Resolve override material (if specified)
    std::shared_ptr<InfMaterial> overrideMat = nullptr;
    if (!overrideMaterial.empty()) {
        overrideMat = AssetRegistry::Instance().GetBuiltinMaterial(overrideMaterial);
    }

    // ---- Collect eligible draw calls (queue filter + frustum cull) ----
    m_eligibleScratch.clear();

    for (const DrawCall &dc : drawCalls()) {
        if (!dc.frustumVisible)
            continue;

        auto material = overrideMat ? overrideMat : (dc.material ? dc.material : defaultMaterial);
        if (!material)
            continue;

        int queue = material->GetRenderQueue();
        if (queue < queueMin || queue > queueMax)
            continue;

        // Pass tag filter: if a pass tag is specified, only draw materials whose
        // passTag matches. Empty passTag on either side means "match all".
        if (!passTag.empty()) {
            const std::string &matTag = material->GetPassTag();
            if (!matTag.empty() && matTag != passTag)
                continue;
        }

        // Compute view-space depth for sorting.
        glm::vec4 viewPos = m_stagedUBO.view * glm::vec4(glm::vec3(dc.worldMatrix[3]), 1.0f);
        float sortKey = viewPos.z;

        // Material + mesh hash for grouping optimization
        size_t matHash = std::hash<void *>{}(material.get());
        auto bufIt = m_perObjectBuffers.find(dc.objectId);
        VkBuffer vb = VK_NULL_HANDLE;
        if (bufIt != m_perObjectBuffers.end() && bufIt->second.vertexBuffer)
            vb = bufIt->second.vertexBuffer->GetBuffer();

        m_eligibleScratch.push_back({&dc, sortKey, matHash, vb, std::move(material), bufIt});
    }

#if INFENGINE_FRAME_PROFILE
    auto stageNow = Clock::now();
    m_drawSubMs[9] += std::chrono::duration<double, std::milli>(stageNow - stageStart).count();
    stageStart = stageNow;
    m_drawSceneFilteredEligible += static_cast<uint64_t>(m_eligibleScratch.size());
#endif

    // One-shot diagnostic: log icon draw calls that pass filtering
    {
        static int s_iconDiagCount = 0;
        if (s_iconDiagCount < 5) {
            for (const auto &entry : m_eligibleScratch) {
                const std::string &matName = entry.material->GetName();
                if (matName.find("GizmoIcon") != std::string::npos || matName.find("Gizmo") != std::string::npos) {
                    const DrawCall &dc = *entry.dc;
                    auto bufIt = m_perObjectBuffers.find(dc.objectId);
                    bool hasBuf = (bufIt != m_perObjectBuffers.end() && bufIt->second.vertexBuffer && bufIt->second.indexBuffer);
                    VkPipeline pip = entry.material->GetPassPipeline(ShaderCompileTarget::Forward);
                    VkDescriptorSet ds = entry.material->GetPassDescriptorSet(ShaderCompileTarget::Forward);
                    INFLOG_INFO("[IconDiag] eligible: mat='", matName,
                                "' queue=", entry.material->GetRenderQueue(),
                                " objId=", dc.objectId,
                                " idxCount=", dc.indexCount,
                                " hasBuf=", hasBuf,
                                " pipeline=", (pip != VK_NULL_HANDLE ? "OK" : "NULL"),
                                " descSet=", (ds != VK_NULL_HANDLE ? "OK" : "NULL"),
                                " blend=", entry.material->GetRenderState().blendEnable,
                                " qRange=[", queueMin, "-", queueMax, "]",
                                " vert='", entry.material->GetVertShaderName(),
                                "' frag='", entry.material->GetFragShaderName(), "'");
                    ++s_iconDiagCount;
                }
            }
        }
    }

    if (m_eligibleScratch.empty()) {
#if INFENGINE_FRAME_PROFILE
        m_drawSubMs[8] += std::chrono::duration<double, std::milli>(Clock::now() - totalStart).count();
#endif
        return;
    }

    // ---- Sort if requested (skip for 0-1 elements) ----
    if (m_eligibleScratch.size() > 1) {
        // In left-handed view space: near objects have small positive Z, far
        // objects have larger positive Z.
        if (sortMode == "front_to_back") {
            // Front-to-back: group by material first (minimizes state changes),
            // then sort by depth within each material group.
            std::sort(m_eligibleScratch.begin(), m_eligibleScratch.end(),
                      [](const SortableDrawCall &a, const SortableDrawCall &b) {
                          if (a.materialHash != b.materialHash)
                              return a.materialHash < b.materialHash;
                          if (a.vertexBuf != b.vertexBuf)
                              return a.vertexBuf < b.vertexBuf;
                          return a.sortKey < b.sortKey;
                      });
        } else if (sortMode == "back_to_front") {
            // Far first → descending Z.
            // Depth order is critical for correct alpha blending.
            std::sort(m_eligibleScratch.begin(), m_eligibleScratch.end(),
                      [](const SortableDrawCall &a, const SortableDrawCall &b) { return a.sortKey > b.sortKey; });
        } else {
            // No explicit sort requested — group by material + mesh for max state reuse
            std::sort(m_eligibleScratch.begin(), m_eligibleScratch.end(),
                      [](const SortableDrawCall &a, const SortableDrawCall &b) {
                          if (a.materialHash != b.materialHash)
                              return a.materialHash < b.materialHash;
                          return a.vertexBuf < b.vertexBuf;
                      });
        }
    } // size() > 1

#if INFENGINE_FRAME_PROFILE
    stageNow = Clock::now();
    m_drawSubMs[10] += std::chrono::duration<double, std::milli>(stageNow - stageStart).count();
    stageStart = stageNow;
#endif

    // ---- Draw loop ----
    VkPipeline currentPipeline = VK_NULL_HANDLE;
    VkPipelineLayout currentLayout = VK_NULL_HANDLE;
    VkDescriptorSet currentDescriptorSet = VK_NULL_HANDLE;
    InfMaterial *currentMaterialRaw = nullptr;
    VkBuffer currentVertexBuffer = VK_NULL_HANDLE;
    uint64_t issuedDraws = 0;

    for (const auto &entry : m_eligibleScratch) {
        const DrawCall &dc = *entry.dc;

        // Material already resolved in filter loop — use directly
        const auto &material = entry.material;
        InfMaterial *matRaw = material.get();

        VkPipeline pipeline = matRaw->GetPassPipeline(ShaderCompileTarget::Forward);
        VkPipelineLayout pipelineLayout = matRaw->GetPassPipelineLayout(ShaderCompileTarget::Forward);

        // Force re-evaluation when material's shader config changed
        if (pipeline != VK_NULL_HANDLE && matRaw->IsPipelineDirty()) {
            pipeline = VK_NULL_HANDLE;
            pipelineLayout = VK_NULL_HANDLE;
        }

        if (pipeline == VK_NULL_HANDLE) {
            const std::string &vertName = matRaw->GetVertShaderName();
            const std::string &fragName = matRaw->GetFragShaderName();
            if (!fragName.empty()) {
                RefreshMaterialPipeline(material, vertName, fragName);
                pipeline = matRaw->GetPassPipeline(ShaderCompileTarget::Forward);
                pipelineLayout = matRaw->GetPassPipelineLayout(ShaderCompileTarget::Forward);
            }
            if (pipeline == VK_NULL_HANDLE && errorMaterial) {
                if (errorMaterial->GetPassPipeline(ShaderCompileTarget::Forward) == VK_NULL_HANDLE) {
                    const std::string &errVert = errorMaterial->GetVertShaderName();
                    const std::string &errFrag = errorMaterial->GetFragShaderName();
                    if (!errFrag.empty()) {
                        RefreshMaterialPipeline(errorMaterial, errVert, errFrag);
                    }
                }
                if (errorMaterial->GetPassPipeline(ShaderCompileTarget::Forward) != VK_NULL_HANDLE) {
                    pipeline = errorMaterial->GetPassPipeline(ShaderCompileTarget::Forward);
                    pipelineLayout = errorMaterial->GetPassPipelineLayout(ShaderCompileTarget::Forward);
                    matRaw = errorMaterial.get();
                }
            }
            if (pipeline == VK_NULL_HANDLE && defaultMaterial) {
                pipeline = defaultMaterial->GetPassPipeline(ShaderCompileTarget::Forward);
                pipelineLayout = defaultMaterial->GetPassPipelineLayout(ShaderCompileTarget::Forward);
                matRaw = defaultMaterial.get();
            }
            if (pipeline == VK_NULL_HANDLE) {
                continue;
            }
        }

        VkDescriptorSet descriptorSet = matRaw->GetPassDescriptorSet(ShaderCompileTarget::Forward);

        if (pipeline != currentPipeline) {
            vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            currentPipeline = pipeline;
        }

        if (matRaw != currentMaterialRaw) {
            UpdateMaterialUBO(*matRaw);
            currentMaterialRaw = matRaw;
            currentLayout = VK_NULL_HANDLE;
            currentDescriptorSet = VK_NULL_HANDLE;
        }

        if (descriptorSet == VK_NULL_HANDLE) {
            static int warnCount = 0;
            if (warnCount++ < 10) {
                INFLOG_WARN("[DrawSceneFiltered] descriptorSet=NULL for material '", matRaw->GetName(),
                            "' queue=", matRaw->GetRenderQueue(),
                            " pipeline=", (pipeline != VK_NULL_HANDLE ? "OK" : "NULL"), " vert='",
                            matRaw->GetVertShaderName(), "' frag='", matRaw->GetFragShaderName(), "'");
            }
            continue;
        }

        if (descriptorSet != currentDescriptorSet || pipelineLayout != currentLayout) {
            vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0,
                                    nullptr);
            currentDescriptorSet = descriptorSet;
            currentLayout = pipelineLayout;

            ShaderProgram *program = matRaw->GetPassShaderProgram(ShaderCompileTarget::Forward);

            if (m_activeShadowDescSet != VK_NULL_HANDLE) {
                // After introducing globals at set 2, pipeline layouts may now
                // contain an auto-created empty gap layout at set 1. Only bind
                // the per-view shadow descriptor when the shader actually
                // declares set 1, otherwise Vulkan rejects the descriptor set as
                // incompatible with the empty layout.
                if (program && program->HasDeclaredDescriptorSet(1)) {
                    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1,
                                            &m_activeShadowDescSet, 0, nullptr);
                }
            }

            if (program && program->HasDeclaredDescriptorSet(2)) {
                const uint32_t frameIndex = m_currentFrame % m_maxFramesInFlight;
                if (frameIndex < m_globalsDescSets.size()) {
                    VkDescriptorSet globalsDescSet = m_globalsDescSets[frameIndex];
                    if (globalsDescSet != VK_NULL_HANDLE) {
                        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 2, 1,
                                                &globalsDescSet, 0, nullptr);
                    }
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

        // Use pre-cached buffer iterator from filter loop
        const auto &bufIt = entry.bufIt;
        if (bufIt != m_perObjectBuffers.end() && bufIt->second.vertexBuffer && bufIt->second.indexBuffer) {
            VkBuffer vb = bufIt->second.vertexBuffer->GetBuffer();
            if (vb != currentVertexBuffer) {
                VkBuffer vertBuffers[] = {vb};
                VkDeviceSize vbOffsets[] = {0};
                vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertBuffers, vbOffsets);
                vkCmdBindIndexBuffer(cmdBuf, bufIt->second.indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
                currentVertexBuffer = vb;
            }
            vkCmdDrawIndexed(cmdBuf, dc.indexCount, 1, dc.indexStart, dc.vertexStart, 0);
            ++issuedDraws;

            // One-shot diagnostic: log when an icon draw call is actually issued
            {
                static int s_iconDrawDiag = 0;
                if (s_iconDrawDiag < 10 && matRaw->GetName().find("Gizmo") != std::string::npos) {
                    INFLOG_INFO("[IconDiag] DRAW ISSUED: mat='", matRaw->GetName(),
                                "' objId=", dc.objectId,
                                " idxCount=", dc.indexCount,
                                " vbuf=", (void*)bufIt->second.vertexBuffer->GetBuffer(),
                                " ibuf=", (void*)bufIt->second.indexBuffer->GetBuffer());
                    ++s_iconDrawDiag;
                }
            }
        } else {
            static int bufWarnCount = 0;
            if (bufWarnCount++ < 10) {
                INFLOG_WARN("[DrawSceneFiltered] no GPU buffers for objectId=", dc.objectId, " material='",
                            matRaw->GetName(), "' queue=", matRaw->GetRenderQueue());
            }
        }
    }

#if INFENGINE_FRAME_PROFILE
    stageNow = Clock::now();
    m_drawSubMs[11] += std::chrono::duration<double, std::milli>(stageNow - stageStart).count();
    m_drawSubMs[8] += std::chrono::duration<double, std::milli>(stageNow - totalStart).count();
    m_drawSceneFilteredIssued += issuedDraws;
#endif
}

// ============================================================================
// Shadow Caster Draw — renders shadow-casting objects with shadow pipeline
// ============================================================================

void InfVkCoreModular::DrawShadowCasters(VkCommandBuffer cmdBuf, uint32_t width, uint32_t height, int queueMin,
                                         int queueMax, int lightIndex, const std::string &shadowType)
{
#if INFENGINE_FRAME_PROFILE
    using Clock = std::chrono::high_resolution_clock;
    const auto totalStart = Clock::now();
    auto stageStart = totalStart;
#endif

    // Currently only light index 0 (first directional light) is supported.
    // Log a warning if callers request a different index so the mismatch is visible.
    if (lightIndex != 0) {
        INFLOG_WARN("DrawShadowCasters: lightIndex=", lightIndex,
                    " requested but only index 0 is supported; using light 0");
    }
    // shadowType is stored for future soft-shadow filtering but not yet consumed.
    (void)shadowType;

    // Skip if shadow pipeline infrastructure not ready (lazy init)
    if (!m_shadowPipelineReady) {
        if (!EnsureShadowPipeline(VK_NULL_HANDLE)) {
            return;
        }
    }

    const uint32_t cascadeCount = m_lightCollector.GetShadowCascadeCount();
    if (cascadeCount == 0)
        return;

#if INFENGINE_FRAME_PROFILE
    ++m_drawShadowCalls;
#endif

    const uint32_t frameIndex = m_currentFrame % m_maxFramesInFlight;

    // Bind shadow pipeline once
    // NOTE: Per-material shadow pipelines override this in the inner loop
    VkPipeline lastBoundPipeline = VK_NULL_HANDLE;

    // Atlas layout: 2x2 tiles inside the full shadow map
    uint32_t tileW = width / 2;
    uint32_t tileH = height / 2;

    // Pre-build draw list (filter once, reuse for all cascades)
    m_shadowDrawScratch.clear();
    m_shadowDrawScratch.reserve(drawCalls().size());
    for (const DrawCall &dc : drawCalls()) {
        if (!dc.material)
            continue;
        int renderQueue = dc.material->GetRenderQueue();
        if (renderQueue < queueMin || renderQueue > queueMax)
            continue;
        auto bufIt = m_perObjectBuffers.find(dc.objectId);
        if (bufIt == m_perObjectBuffers.end() || !bufIt->second.vertexBuffer || !bufIt->second.indexBuffer)
            continue;

        VkPipeline pip = dc.material->GetPassPipeline(ShaderCompileTarget::Shadow);
        if (pip == VK_NULL_HANDLE) {
            // Lazy creation: shadow shared resources are ready, create per-material pipeline now
            CreateMaterialShadowPipeline(dc.material, dc.material->GetVertShaderName(),
                                         dc.material->GetFragShaderName());
            pip = dc.material->GetPassPipeline(ShaderCompileTarget::Shadow);
        }
        if (pip == VK_NULL_HANDLE)
            continue; // no per-material shadow pipeline available, skip
        m_shadowDrawScratch.push_back({&dc, bufIt, pip, dc.worldBounds});
    }

#if INFENGINE_FRAME_PROFILE
    auto stageNow = Clock::now();
    m_drawSubMs[13] += std::chrono::duration<double, std::milli>(stageNow - stageStart).count();
    stageStart = stageNow;
    m_drawShadowEligible += static_cast<uint64_t>(m_shadowDrawScratch.size());
#endif

    if (m_shadowDrawScratch.empty()) {
        static int s_noShadowDrawsWarnCount = 0;
        if (s_noShadowDrawsWarnCount++ < 8) {
            INFLOG_WARN("DrawShadowCasters: no drawable shadow casters were collected (drawCalls=", drawCalls().size(),
                        ", queueRange=", queueMin, "-", queueMax, ", cascades=", cascadeCount, ")");
        }

#if INFENGINE_FRAME_PROFILE
        m_drawSubMs[12] += std::chrono::duration<double, std::milli>(Clock::now() - totalStart).count();
#endif
    }

    uint64_t issuedDraws = 0;

    // Pre-extract frustums for all cascades (for per-cascade AABB culling)
    Frustum cascadeFrustums[NUM_SHADOW_CASCADES];
    for (uint32_t ci = 0; ci < cascadeCount && ci < NUM_SHADOW_CASCADES; ++ci) {
        cascadeFrustums[ci].ExtractFromMatrix(m_lightCollector.GetShadowLightVP(ci));
    }

    for (uint32_t ci = 0; ci < cascadeCount; ++ci) {
        uint32_t descIdx = frameIndex * NUM_SHADOW_CASCADES + ci;
        if (descIdx >= m_shadowDescSets.size())
            break;

        // Set viewport/scissor to cascade's atlas tile
        uint32_t tileX = (ci % 2) * tileW;
        uint32_t tileY = (ci / 2) * tileH;

        VkViewport viewport{};
        viewport.x = static_cast<float>(tileX);
        viewport.y = static_cast<float>(tileY);
        viewport.width = static_cast<float>(tileW);
        viewport.height = static_cast<float>(tileH);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {static_cast<int32_t>(tileX), static_cast<int32_t>(tileY)};
        scissor.extent = {tileW, tileH};
        vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

        // Bind per-cascade descriptor set
        VkDescriptorSet cascadeDescSet = m_shadowDescSets[descIdx];
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_shadowPipelineLayout, 0, 1, &cascadeDescSet,
                                0, nullptr);

        const Frustum &cascadeFrustum = cascadeFrustums[ci];
        VkBuffer currentVertexBuffer = VK_NULL_HANDLE;
        for (const auto &sd : m_shadowDrawScratch) {
            // Per-cascade frustum culling: skip objects outside this cascade's light-space volume
            if (sd.worldBounds.IsValid() && !cascadeFrustum.IntersectsAABB(sd.worldBounds))
                continue;

            // Bind per-material shadow pipeline (or global fallback)
            if (sd.shadowPipeline != lastBoundPipeline) {
                vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, sd.shadowPipeline);
                lastBoundPipeline = sd.shadowPipeline;
            }

            struct PushData
            {
                glm::mat4 model;
                glm::mat4 normalMat;
            } pushData;
            pushData.model = sd.dc->worldMatrix;
            // Shadow pass only writes depth — normalMat is unused by the shader.
            // Skip the expensive glm::inverse() computation (was 4000+ calls/frame).
            pushData.normalMat = glm::mat4(1.0f);

            vkCmdPushConstants(cmdBuf, m_shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushData),
                               &pushData);

            VkBuffer vb = sd.bufIt->second.vertexBuffer->GetBuffer();
            if (vb != currentVertexBuffer) {
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(cmdBuf, 0, 1, &vb, offsets);
                vkCmdBindIndexBuffer(cmdBuf, sd.bufIt->second.indexBuffer->GetBuffer(), 0, VK_INDEX_TYPE_UINT32);
                currentVertexBuffer = vb;
            }

            vkCmdDrawIndexed(cmdBuf, sd.dc->indexCount, 1, sd.dc->indexStart, sd.dc->vertexStart, 0);
            ++issuedDraws;
        }
    }

#if INFENGINE_FRAME_PROFILE
    stageNow = Clock::now();
    m_drawSubMs[14] += std::chrono::duration<double, std::milli>(stageNow - stageStart).count();
    m_drawSubMs[12] += std::chrono::duration<double, std::milli>(stageNow - totalStart).count();
    m_drawShadowIssued += issuedDraws;
#endif
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

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependency.dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &depthAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

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

    // --- Create descriptor pool (frames × cascades) ---
    const uint32_t totalSets = m_maxFramesInFlight * NUM_SHADOW_CASCADES;
    if (m_shadowDescPool == VK_NULL_HANDLE) {
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = totalSets;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = totalSets;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;

        if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_shadowDescPool) != VK_SUCCESS) {
            INFLOG_ERROR("Failed to create shadow descriptor pool");
            return false;
        }
    }

    // --- Allocate descriptor sets (frames × cascades) ---
    if (m_shadowDescSets.empty()) {
        m_shadowDescSets.resize(totalSets, VK_NULL_HANDLE);
        std::vector<VkDescriptorSetLayout> setLayouts(totalSets, m_shadowDescSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_shadowDescPool;
        allocInfo.descriptorSetCount = totalSets;
        allocInfo.pSetLayouts = setLayouts.data();

        if (vkAllocateDescriptorSets(device, &allocInfo, m_shadowDescSets.data()) != VK_SUCCESS) {
            INFLOG_ERROR("Failed to allocate shadow descriptor sets");
            return false;
        }
    }

    // --- Create per-frame per-cascade UBO buffers ---
    if (m_shadowUboBuffers.empty()) {
        VkDeviceSize uboSize = sizeof(glm::mat4) * 3;

        m_shadowUboBuffers.resize(totalSets, VK_NULL_HANDLE);
        m_shadowUboAllocations.resize(totalSets, VK_NULL_HANDLE);
        m_shadowUboMappedPtrs.resize(totalSets, nullptr);

        VmaAllocator allocator = m_deviceContext.GetVmaAllocator();
        for (uint32_t i = 0; i < totalSets; ++i) {
            VkBufferCreateInfo bufInfo{};
            bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufInfo.size = uboSize;
            bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocCreateInfo{};
            allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            VmaAllocationInfo vmaAllocInfo{};
            VkResult result = vmaCreateBuffer(allocator, &bufInfo, &allocCreateInfo, &m_shadowUboBuffers[i],
                                              &m_shadowUboAllocations[i], &vmaAllocInfo);
            if (result != VK_SUCCESS) {
                INFLOG_ERROR("Failed to create shadow UBO buffer via VMA");
                return false;
            }
            m_shadowUboMappedPtrs[i] = vmaAllocInfo.pMappedData;

            VkDescriptorBufferInfo bufDesc{};
            bufDesc.buffer = m_shadowUboBuffers[i];
            bufDesc.offset = 0;
            bufDesc.range = uboSize;

            VkWriteDescriptorSet write{};
            write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet = m_shadowDescSets[i];
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

    // --- Create shadow pipeline layout (shared by all per-material shadow pipelines) ---
    if (m_shadowPipelineLayout == VK_NULL_HANDLE) {
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
    }

    m_shadowPipelineReady = true;
    INFLOG_INFO("Shadow pipeline infrastructure created successfully");
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

    const SharedMeshKey sharedKey{vertices.data(), indices.data()};

    auto objectIt = m_perObjectBuffers.find(objectId);
    if (objectIt != m_perObjectBuffers.end() && !forceUpdate) {
        if (objectIt->second.sharedKey == sharedKey && objectIt->second.vertexCount == vertices.size() &&
            objectIt->second.indexCount == indices.size()) {
            return;
        }
    }

    auto sharedIt = m_sharedMeshBuffers.find(sharedKey);
    const bool needsCreate = (sharedIt == m_sharedMeshBuffers.end() || forceUpdate ||
                              sharedIt->second.vertexCount != vertices.size() ||
                              sharedIt->second.indexCount != indices.size() || !sharedIt->second.vertexBuffer ||
                              !sharedIt->second.indexBuffer);

    if (needsCreate) {
        SharedMeshBuffers sharedBuffers;
        sharedBuffers.vertexBuffer = std::shared_ptr<vk::VkBufferHandle>(
            m_resourceManager.CreateVertexBuffer(vertices.data(), vertices.size() * sizeof(Vertex)).release());
        sharedBuffers.indexBuffer = std::shared_ptr<vk::VkBufferHandle>(
            m_resourceManager.CreateIndexBuffer(indices.data(), indices.size() * sizeof(uint32_t)).release());
        sharedBuffers.vertexCount = vertices.size();
        sharedBuffers.indexCount = indices.size();
        sharedIt = m_sharedMeshBuffers.insert_or_assign(sharedKey, std::move(sharedBuffers)).first;
    }

    PerObjectBuffers objectBuffers;
    objectBuffers.vertexBuffer = sharedIt->second.vertexBuffer;
    objectBuffers.indexBuffer = sharedIt->second.indexBuffer;
    objectBuffers.vertexCount = sharedIt->second.vertexCount;
    objectBuffers.indexCount = sharedIt->second.indexCount;
    objectBuffers.sharedKey = sharedKey;

    m_perObjectBuffers[objectId] = std::move(objectBuffers);
}

void InfVkCoreModular::CleanupUnusedBuffers(const std::vector<DrawCall> &activeDrawCalls)
{
    // Build set of active objectIds
    std::unordered_set<uint64_t> activeIds;
    std::unordered_set<SharedMeshKey, SharedMeshKeyHash> activeSharedKeys;
    for (const auto &dc : activeDrawCalls) {
        activeIds.insert(dc.objectId);
        if (dc.meshVertices && dc.meshIndices && !dc.meshVertices->empty() && !dc.meshIndices->empty()) {
            activeSharedKeys.insert({dc.meshVertices->data(), dc.meshIndices->data()});
        }
    }

    // Remove buffers for objects no longer in the scene.
    // Actual GPU resource destruction is deferred via FrameDeletionQueue
    // so that in-flight command buffers are never invalidated.
    for (auto it = m_perObjectBuffers.begin(); it != m_perObjectBuffers.end();) {
        if (activeIds.find(it->first) == activeIds.end()) {
            it = m_perObjectBuffers.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = m_sharedMeshBuffers.begin(); it != m_sharedMeshBuffers.end();) {
        if (activeSharedKeys.find(it->first) == activeSharedKeys.end()) {
            auto buffers = std::make_shared<SharedMeshBuffers>(std::move(it->second));
            m_deletionQueue.Push([buffers]() mutable {
                buffers->vertexBuffer.reset();
                buffers->indexBuffer.reset();
            });
            it = m_sharedMeshBuffers.erase(it);
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
                                              VkSampler shadowSampler, VkImageLayout imageLayout)
{
    if (perViewDescSet == VK_NULL_HANDLE || shadowView == VK_NULL_HANDLE || shadowSampler == VK_NULL_HANDLE)
        return;

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = imageLayout;
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

    UpdatePerViewShadowMap(perViewDescSet, defaultView, defaultSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

} // namespace infengine
