/**
 * @file SceneRenderGraph.cpp
 * @brief Implementation of RenderGraph-based scene rendering
 *
 * This implementation fully utilizes vk::RenderGraph for all rendering.
 * No more imperative BeginRenderPass/EndRenderPass calls.
 */

#include "SceneRenderGraph.h"
#include "FullscreenRenderer.h"
#include "InfVkCoreModular.h"
#include "SceneRenderTarget.h"
#include "gui/InfScreenUIRenderer.h"
#include "vk/VkDeviceContext.h"
#include "vk/VkPipelineManager.h"
#include <algorithm>
#include <core/error/InfError.h>
#include <function/resources/InfMaterial/InfMaterial.h>
#include <function/scene/Camera.h>

namespace infengine
{

// ============================================================================
// Constructor / Destructor
// ============================================================================

SceneRenderGraph::SceneRenderGraph() : m_renderGraph(std::make_unique<vk::RenderGraph>())
{
}

SceneRenderGraph::~SceneRenderGraph()
{
    Destroy();
}

// ============================================================================
// Initialization
// ============================================================================

bool SceneRenderGraph::Initialize(InfVkCoreModular *vkCore, SceneRenderTarget *sceneTarget)
{
    if (!vkCore || !sceneTarget) {
        INFLOG_ERROR("SceneRenderGraph::Initialize: Invalid parameters");
        return false;
    }

    m_vkCore = vkCore;
    m_sceneTarget = sceneTarget;
    m_width = sceneTarget->GetWidth();
    m_height = sceneTarget->GetHeight();

    // Initialize the underlying RenderGraph with device context and pipeline manager
    m_renderGraph->Initialize(&vkCore->GetDeviceContext(), &vkCore->GetPipelineManager());

    // Allocate per-graph shadow descriptor set (set 1) for multi-camera isolation
    m_perViewDescSet = vkCore->AllocatePerViewDescriptorSet();
    if (m_perViewDescSet == VK_NULL_HANDLE) {
        INFLOG_WARN("SceneRenderGraph: Failed to allocate per-view descriptor set");
    }

    // Initialize fullscreen effect renderer for FullscreenQuad passes
    m_fullscreenRenderer.Initialize(vkCore);

    INFLOG_INFO("SceneRenderGraph initialized with full RenderGraph: ", m_width, "x", m_height);
    return true;
}

void SceneRenderGraph::Destroy()
{
    m_fullscreenRenderer.Destroy();
    m_transientResources.clear();

    if (m_renderGraph) {
        m_renderGraph->Destroy();
    }
    m_importedColorTarget = {};
    m_importedDepthTarget = {};
    m_graphBuilt = false;
    m_vkCore = nullptr;
    m_sceneTarget = nullptr;
}

// ============================================================================
// Resource Management (Phase 0)
// ============================================================================

vk::ResourceHandle SceneRenderGraph::CreateTransientTexture(const std::string &name, uint32_t width, uint32_t height,
                                                            VkFormat format, bool isTransient)
{
    if (!m_renderGraph) {
        INFLOG_ERROR("SceneRenderGraph::CreateTransientTexture: RenderGraph not initialized");
        return {};
    }

    // Check if resource already exists
    auto it = m_transientResources.find(name);
    if (it != m_transientResources.end()) {
        INFLOG_WARN("SceneRenderGraph::CreateTransientTexture: Resource '", name,
                    "' already exists, returning existing handle");
        return it->second;
    }

    // ========================================================================
    // Bug 5 fix: allocate a real ResourceData entry in the underlying
    // RenderGraph so that the returned handle can be resolved by
    // ResolveTextureView().  The previous code fabricated an id with a
    // base-1000 offset that had no backing ResourceData — any call to
    // ResolveTextureView() with such a handle would access out-of-bounds
    // memory.
    // ========================================================================
    vk::ResourceHandle handle =
        m_renderGraph->RegisterTransientTexture(name, width, height, format, VK_SAMPLE_COUNT_1_BIT, isTransient);

    m_transientResources[name] = handle;
    m_needsRebuild = true;

    INFLOG_DEBUG("SceneRenderGraph: Created transient texture '", name, "' id=", handle.id, " (", width, "x", height,
                 ", format ", static_cast<int>(format), ")");

    return handle;
}

// ============================================================================
// Phase 2: Python-Driven RenderGraph Topology
// ============================================================================

void SceneRenderGraph::ApplyPythonGraph(const RenderGraphDescription &desc)
{
    if (!m_vkCore || !m_sceneTarget) {
        INFLOG_ERROR("SceneRenderGraph::ApplyPythonGraph: Not initialized");
        return;
    }

    // Clear previous Python-driven state.
    m_pythonCallbacks.clear();

    // Capture vkCore for pass lambdas (avoids capturing 'this')
    InfVkCoreModular *vkCore = m_vkCore;

    for (const auto &passDesc : desc.passes) {
        // Build the render callback directly from the pass action.
        const auto graphPassAction = passDesc.action;
        const int queueMin = passDesc.queueMin;
        const int queueMax = passDesc.queueMax;
        const std::string computeShaderName = passDesc.computeShaderName;
        const uint32_t dispatchX = passDesc.dispatchX;
        const uint32_t dispatchY = passDesc.dispatchY;
        const uint32_t dispatchZ = passDesc.dispatchZ;
        const int screenUIListIndex = passDesc.screenUIList;

        // Capture input bindings for passes that need graph texture access
        // (e.g. reading shadow map as a sampled texture).
        const auto inputBindings = passDesc.inputBindings;

        // Capture screen UI renderer pointer for DrawScreenUI passes
        InfScreenUIRenderer *screenUIRenderer = m_screenUIRenderer;

        m_pythonCallbacks[passDesc.name] = [vkCore, graphPassAction, queueMin, queueMax, computeShaderName, dispatchX,
                                            dispatchY, dispatchZ, screenUIRenderer, screenUIListIndex,
                                            inputBindings](vk::RenderContext &ctx, uint32_t w, uint32_t h) {
            switch (graphPassAction) {
            case GraphPassActionType::DrawRenderers:
                vkCore->DrawSceneFiltered(ctx.GetCommandBuffer(), w, h, queueMin, queueMax);
                break;
            case GraphPassActionType::DrawSkybox:
                // Skybox occupies render queue 32767 — draw via filtered call
                vkCore->DrawSceneFiltered(ctx.GetCommandBuffer(), w, h, 32767, 32767);
                break;
            case GraphPassActionType::Compute:
                vkCmdDispatch(ctx.GetCommandBuffer(), dispatchX, dispatchY, dispatchZ);
                break;
            case GraphPassActionType::DrawShadowCasters:
                // Shadow caster pass: draw filtered objects using shadow pipeline
                // with lightVP from SceneLightCollector. The shadow pipeline is
                // lazily created inside DrawShadowCasters().
                vkCore->DrawShadowCasters(ctx.GetCommandBuffer(), w, h, queueMin, queueMax);
                break;
            case GraphPassActionType::DrawScreenUI:
                if (screenUIRenderer) {
                    auto list = (screenUIListIndex == 0) ? ScreenUIList::Camera : ScreenUIList::Overlay;
                    screenUIRenderer->Render(ctx.GetCommandBuffer(), list, w, h);
                }
                break;
            case GraphPassActionType::FullscreenQuad:
                // FullscreenQuad passes are handled entirely inside
                // BuildRenderGraph's execute lambda — the callback is a
                // no-op placeholder so the pass entry exists in m_pythonCallbacks.
                break;
            default:
                break;
            }
        };
    }

    // ========================================================================
    // Auto-append _ComponentGizmos pass (queue 10000-20000).
    // Python-defined per-component gizmos, rendered with depth testing
    // against existing scene geometry. Runs before editor gizmos.
    // ========================================================================
    static constexpr int COMP_GIZMO_QUEUE_MIN = 10000;
    static constexpr int COMP_GIZMO_QUEUE_MAX = 20000;
    static const std::string kComponentGizmosPassName = "_ComponentGizmos";
    m_pythonCallbacks[kComponentGizmosPassName] = [vkCore](vk::RenderContext &ctx, uint32_t w, uint32_t h) {
        vkCore->DrawSceneFiltered(ctx.GetCommandBuffer(), w, h, COMP_GIZMO_QUEUE_MIN, COMP_GIZMO_QUEUE_MAX);
    };

    // ========================================================================
    // Auto-append editor gizmos pass (queue 20001-25000).
    // This ensures grid/gizmos always render after all user-defined passes,
    // regardless of what queue ranges the user pipeline declares.
    // In game view (no gizmo draw calls), DrawSceneFiltered finds nothing
    // in this range and the pass is effectively a no-op.
    // ========================================================================
    static constexpr int GIZMO_QUEUE_MIN = 20001;
    static constexpr int GIZMO_QUEUE_MAX = 25000;
    static const std::string kEditorGizmosPassName = "_EditorGizmos";
    m_pythonCallbacks[kEditorGizmosPassName] = [vkCore](vk::RenderContext &ctx, uint32_t w, uint32_t h) {
        vkCore->DrawSceneFiltered(ctx.GetCommandBuffer(), w, h, GIZMO_QUEUE_MIN, GIZMO_QUEUE_MAX);
    };

    // ========================================================================
    // Auto-append editor tools pass (queue 25001-30000).
    // Translation/rotation/scale handles rendered on top of everything
    // (no depth test). In game view, no draw calls exist in this range.
    // ========================================================================
    static constexpr int TOOLS_QUEUE_MIN = 25001;
    static constexpr int TOOLS_QUEUE_MAX = 30000;
    static const std::string kEditorToolsPassName = "_EditorTools";
    m_pythonCallbacks[kEditorToolsPassName] = [vkCore](vk::RenderContext &ctx, uint32_t w, uint32_t h) {
        vkCore->DrawSceneFiltered(ctx.GetCommandBuffer(), w, h, TOOLS_QUEUE_MIN, TOOLS_QUEUE_MAX);
    };

    // Store description for BuildRenderGraph()'s topology traversal.
    // Only trigger a rebuild if the graph topology actually changed.
    // ApplyPythonGraph is called every frame; avoid vkDeviceWaitIdle +
    // full resource teardown when the description is identical.
    bool topologyChanged = !m_hasPythonGraph || (desc.passes.size() != m_pythonGraphDesc.passes.size()) ||
                           (desc.textures.size() != m_pythonGraphDesc.textures.size()) ||
                           (desc.outputTexture != m_pythonGraphDesc.outputTexture);
    if (!topologyChanged) {
        // Quick content check: compare pass names, actions, shader names,
        // and push constants.  FullscreenQuad push constants are captured at
        // build time inside lambdas, so value changes require a rebuild.
        for (size_t i = 0; i < desc.passes.size(); ++i) {
            if (desc.passes[i].name != m_pythonGraphDesc.passes[i].name ||
                desc.passes[i].action != m_pythonGraphDesc.passes[i].action ||
                desc.passes[i].shaderName != m_pythonGraphDesc.passes[i].shaderName ||
                desc.passes[i].pushConstants != m_pythonGraphDesc.passes[i].pushConstants) {
                topologyChanged = true;
                break;
            }
        }
    }

    m_pythonGraphDesc = desc;
    m_hasPythonGraph = true;
    if (topologyChanged) {
        m_needsRebuild = true;
    }
}

// ============================================================================
// Execution (Pure RenderGraph)
// ============================================================================

void SceneRenderGraph::Execute(VkCommandBuffer commandBuffer)
{
    if (!m_sceneTarget || !m_sceneTarget->IsReady() || !m_renderGraph) {
        return;
    }

    // Update dimensions if changed
    if (m_width != m_sceneTarget->GetWidth() || m_height != m_sceneTarget->GetHeight()) {
        m_width = m_sceneTarget->GetWidth();
        m_height = m_sceneTarget->GetHeight();
        m_needsRebuild = true;
    }

    // Rebuild RenderGraph if needed
    if (m_needsRebuild) {
        BuildRenderGraph();
        m_needsRebuild = false;
        m_needsCompile = true; // Need to compile after rebuild
    }

    // Compile and execute the render graph
    if (m_graphBuilt) {
        // Only compile when needed (after rebuild or first time)
        if (m_needsCompile) {
            if (!m_renderGraph->Compile()) {
                INFLOG_ERROR("SceneRenderGraph: Failed to compile render graph");
                return;
            }
            m_needsCompile = false;
        }

        // ====================================================================
        // Per-frame camera clear-value override (Bug 3 / Bug 7 fix).
        //
        // Apply clear COLOR values to the compiled passes without rebuilding
        // the graph.  This covers:
        //   - SolidColor background color changes (every-frame update)
        //   - Multi-camera rendering (each camera calls UpdateMainPassClearSettings
        //     then Execute; per-frame update ensures the correct camera's
        //     clear values are used).
        // ====================================================================
        if (m_hasCameraClearOverride && !m_mainClearPassName.empty()) {
            if (m_cameraClearFlags == CameraClearFlags::Skybox) {
                m_renderGraph->UpdatePassClearColor(m_mainClearPassName, 0.0f, 0.0f, 0.0f, 1.0f);
            } else if (m_cameraClearFlags == CameraClearFlags::SolidColor) {
                m_renderGraph->UpdatePassClearColor(m_mainClearPassName, m_cameraBgColor.r, m_cameraBgColor.g,
                                                    m_cameraBgColor.b, m_cameraBgColor.a);
            }
            // DepthOnly / DontClear: color clear is disabled at build time
            // (clearColorEnabled == false), so updating the value is a no-op.
        }

        // Snapshot current clear state so next frame's UpdateMainPassClearSettings
        // can detect *loadOp* changes vs mere value changes.
        m_prevClearStateValid = true;
        m_prevCameraClearFlags = m_cameraClearFlags;
        m_prevCameraBgColor = m_cameraBgColor;

        // Reset fullscreen descriptor pool for this frame
        m_fullscreenRenderer.ResetPool();

        m_renderGraph->Execute(commandBuffer);
    }
}

void SceneRenderGraph::OnResize(uint32_t width, uint32_t height)
{
    if (m_width != width || m_height != height) {
        m_width = width;
        m_height = height;
        m_needsRebuild = true;
        m_graphBuilt = false; // Force complete rebuild

        INFLOG_DEBUG("SceneRenderGraph: Resized to ", width, "x", height);
    }
}

// ============================================================================
// Debug
// ============================================================================

std::string SceneRenderGraph::GetDebugString() const
{
    std::string result =
        "SceneRenderGraph [RenderGraph Mode] (" + std::to_string(m_width) + "x" + std::to_string(m_height) + ")\n";
    result += "Graph Built: " + std::string(m_graphBuilt ? "Yes" : "No") + "\n";
    result += "Python Graph: " + std::string(m_hasPythonGraph ? "Yes" : "No") + "\n";
    if (m_hasPythonGraph) {
        result += "Passes (" + std::to_string(m_pythonGraphDesc.passes.size()) + "):\n";
        for (const auto &pass : m_pythonGraphDesc.passes) {
            result += "  " + pass.name + "\n";
        }
    }

    // Add underlying RenderGraph debug info
    if (m_renderGraph && m_graphBuilt) {
        result += "\nUnderlying RenderGraph:\n";
        result += m_renderGraph->GetDebugString();
    }

    return result;
}

// ============================================================================
// Pass Output Access
// ============================================================================

std::vector<std::string> SceneRenderGraph::GetReadablePassNames() const
{
    // Readback is currently only supported through RenderPassOutput objects,
    // which are not yet used by the Python graph path.
    return {};
}

bool SceneRenderGraph::ReadPassColorPixels(const std::string &passName, std::vector<uint8_t> &outData)
{
    // TODO: Implement readback via RenderGraph transient resources
    INFLOG_WARN("SceneRenderGraph::ReadPassColorPixels: not yet supported in Python graph path");
    return false;
}

bool SceneRenderGraph::ReadPassDepthPixels(const std::string &passName, std::vector<float> &outData)
{
    // TODO: Implement readback via RenderGraph transient resources
    INFLOG_WARN("SceneRenderGraph::ReadPassDepthPixels: not yet supported in Python graph path");
    return false;
}

bool SceneRenderGraph::GetPassOutputSize(const std::string &passName, uint32_t &outWidth, uint32_t &outHeight) const
{
    outWidth = m_width;
    outHeight = m_height;
    return m_graphBuilt;
}

uint64_t SceneRenderGraph::GetPassTextureId(const std::string &passName) const
{
    // TODO: Implement texture ID retrieval via RenderGraph transient resources
    return 0;
}

// ============================================================================
// Private Methods
// ============================================================================

void SceneRenderGraph::ImportSceneTargetResources()
{
    if (!m_sceneTarget || !m_renderGraph) {
        return;
    }

    // Import the color image as the backbuffer.
    // When MSAA is enabled, GetMsaaColorImage() returns the Nx multisampled image.
    // When MSAA is off (1x), it returns the same 1x color image.
    m_importedColorTarget = m_renderGraph->SetBackbuffer(
        m_sceneTarget->GetMsaaColorImage(), m_sceneTarget->GetMsaaColorImageView(), m_sceneTarget->GetColorFormat(),
        m_width, m_height, m_sceneTarget->GetMsaaSampleCount());

    // Import the 1x resolve target only when MSAA is active
    if (m_sceneTarget->IsMsaaEnabled()) {
        m_importedResolveTarget =
            m_renderGraph->ImportResolveTarget(m_sceneTarget->GetColorImage(), m_sceneTarget->GetColorImageView(),
                                               m_sceneTarget->GetColorFormat(), m_width, m_height);
    } else {
        m_importedResolveTarget = {}; // Clear — no separate resolve target needed
    }
}

void SceneRenderGraph::UpdateMainPassClearSettings(CameraClearFlags clearFlags, const glm::vec4 &bgColor)
{
    m_hasCameraClearOverride = true;
    m_cameraClearFlags = clearFlags;
    m_cameraBgColor = bgColor;

    // Detect loadOp changes (Skybox↔SolidColor↔DepthOnly↔DontClear)
    // which require a full graph rebuild. Mere color value changes are
    // applied per-frame in Execute() without rebuild.
    if (m_prevClearStateValid && m_prevCameraClearFlags != clearFlags) {
        m_needsRebuild = true;
    }
}

void SceneRenderGraph::ResolveSceneMsaa(VkCommandBuffer commandBuffer)
{
    if (!m_sceneTarget) {
        return;
    }

    // When MSAA is disabled (1x), the backbuffer IS the color image —
    // no resolve needed; just transition to SHADER_READ_ONLY for ImGui.
    if (!m_sceneTarget->IsMsaaEnabled()) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_sceneTarget->GetColorImage();
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        return;
    }

    VkImage msaaImage = m_sceneTarget->GetMsaaColorImage();
    VkImage resolveImage = m_sceneTarget->GetColorImage();

    // Barrier: MSAA color (4x) from COLOR_ATTACHMENT_OPTIMAL → TRANSFER_SRC_OPTIMAL
    // 1x color from UNDEFINED → TRANSFER_DST_OPTIMAL
    {
        VkImageMemoryBarrier barriers[2]{};

        // MSAA source
        barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].image = msaaImage;
        barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barriers[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        // 1x resolve destination
        barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].image = resolveImage;
        barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barriers[1].srcAccessMask = 0;
        barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
    }

    // Resolve MSAA → 1x
    VkImageResolve resolveRegion{};
    resolveRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    resolveRegion.srcOffset = {0, 0, 0};
    resolveRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    resolveRegion.dstOffset = {0, 0, 0};
    resolveRegion.extent = {m_width, m_height, 1};

    vkCmdResolveImage(commandBuffer, msaaImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, resolveImage,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &resolveRegion);

    // Post-resolve barriers:
    // 1. MSAA color: TRANSFER_SRC → COLOR_ATTACHMENT_OPTIMAL
    //    Keeps the layout consistent with the RenderGraph's m_resourceStates
    //    tracking, which records COLOR_ATTACHMENT_OPTIMAL after the render pass.
    //    Without this, frame 2+ barriers would have incorrect oldLayout.
    // 2. 1x resolve: TRANSFER_DST → COLOR_ATTACHMENT_OPTIMAL
    //    Needed by outline composite pass or no-outline barrier.
    {
        VkImageMemoryBarrier barriers[2]{};

        // MSAA source: restore to COLOR_ATTACHMENT_OPTIMAL
        barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[0].image = msaaImage;
        barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        // 1x resolve destination: ready for outline / ImGui sampling
        barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barriers[1].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[1].image = resolveImage;
        barriers[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barriers[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 2, barriers);
    }
}

void SceneRenderGraph::BuildRenderGraph()
{
    if (!m_renderGraph || !m_sceneTarget || !m_vkCore) {
        INFLOG_WARN("SceneRenderGraph::BuildRenderGraph - Missing required components");
        return;
    }

    // Reset the render graph for rebuild.
    // FreeResources() inside Reset() may destroy transient VkImageViews
    // (e.g. shadow map). Clear stale shadow map references from per-view
    // descriptor AFTER the GPU is idle (Reset waits) but BEFORE any new
    // rendering, to prevent VK_ERROR_DEVICE_LOST from sampling a destroyed view.
    m_renderGraph->Reset();
    if (m_perViewDescSet != VK_NULL_HANDLE) {
        m_vkCore->ClearPerViewShadowMap(m_perViewDescSet);
    }
    m_graphBuilt = false;

    // Skip if no Python graph has been configured.
    if (!m_hasPythonGraph) {
        INFLOG_DEBUG("SceneRenderGraph::BuildRenderGraph - No Python graph configured");
        return;
    }

    // Import scene target resources as external resources
    ImportSceneTargetResources();

    // Custom RT tracking: maps texture name → ResourceHandle for
    // non-backbuffer color textures.
    std::unordered_map<std::string, vk::ResourceHandle> customRTHandles;

    // ========================================================================
    // Python-driven graph: create independent passes with shared resources
    // ========================================================================
    if (!m_pythonGraphDesc.passes.empty()) {
        // Build texture name → desc map
        std::unordered_map<std::string, const GraphTextureDesc *> texDescMap;
        for (const auto &tex : m_pythonGraphDesc.textures) {
            texDescMap[tex.name] = &tex;
        }

        // Iterate in declaration order — the RenderGraph's Kahn topological
        // sort handles execution ordering via DAG dependencies.
        const auto &sortedPasses = m_pythonGraphDesc.passes;

        uint32_t width = m_width;
        uint32_t height = m_height;
        VkFormat depthFormat = m_sceneTarget->GetDepthFormat();
        VkSampleCountFlagBits msaaSamples = m_sceneTarget->GetMsaaSampleCount();

        // Capture vkCore for pass lambdas (avoids capturing 'this')
        InfVkCoreModular *vkCore = m_vkCore;

        // Shared depth handle — created by the first pass that writes depth,
        // referenced by later passes via ReadDepth().
        vk::ResourceHandle sharedDepth;

        // =================================================================
        // Custom RT tracking: Non-backbuffer color textures get a transient
        // resource created by the first pass that writes to them. Later
        // passes can read them via builder.Read() for proper DAG edges.
        // =================================================================

        // Pre-register transient textures for non-backbuffer, non-depth
        // textures so their ResourceHandle is available before passes reference them.
        for (const auto &tex : m_pythonGraphDesc.textures) {
            if (!tex.isBackbuffer && !tex.isDepth) {
                // Use custom size if specified, otherwise use scene target dimensions
                uint32_t texW = (tex.width > 0) ? tex.width : width;
                uint32_t texH = (tex.height > 0) ? tex.height : height;
                // Apply sizeDivisor: divide scene dimensions when divisor > 1
                if (tex.sizeDivisor > 1) {
                    texW = std::max(1u, width / tex.sizeDivisor);
                    texH = std::max(1u, height / tex.sizeDivisor);
                }
                vk::ResourceHandle handle = m_renderGraph->RegisterTransientTexture(tex.name, texW, texH, tex.format,
                                                                                    VK_SAMPLE_COUNT_1_BIT, true);
                customRTHandles[tex.name] = handle;
            }
        }

        // Pre-register custom-size depth textures (shadow maps).
        // These are depth textures with explicit dimensions — they need
        // ResourceHandles registered upfront so later passes can reference
        // them via inputBindings (e.g. shadow map bound as a sampled texture).
        for (const auto &tex : m_pythonGraphDesc.textures) {
            if (tex.isDepth && tex.width > 0 && tex.height > 0) {
                vk::ResourceHandle handle = m_renderGraph->RegisterTransientTexture(
                    tex.name, tex.width, tex.height, tex.format, VK_SAMPLE_COUNT_1_BIT, true);
                customRTHandles[tex.name] = handle;
            }
        }

        // Pre-scan: find the last SCENE pass (DrawRenderers / DrawSkybox) that
        // writes to the MSAA backbuffer. Only scene passes get the subpass
        // resolve.
        std::string lastBackbufferPassName;
        for (const auto &passDesc : sortedPasses) {
            if (m_pythonCallbacks.count(passDesc.name) == 0) {
                continue;
            }
            // Only scene passes (DrawRenderers, DrawSkybox) qualify for MSAA resolve
            if (passDesc.action != GraphPassActionType::DrawRenderers &&
                passDesc.action != GraphPassActionType::DrawSkybox) {
                continue;
            }
            bool writesToBackbuffer = true;
            // Check the primary (slot 0) color output
            for (const auto &[slot, texName] : passDesc.writeColors) {
                if (slot == 0 && !texName.empty()) {
                    auto texIt = texDescMap.find(texName);
                    if (texIt != texDescMap.end() && !texIt->second->isBackbuffer) {
                        writesToBackbuffer = false;
                    }
                    break;
                }
            }
            if (writesToBackbuffer) {
                lastBackbufferPassName = passDesc.name;
            }
        }

        // Track whether an MSAA→1x resolve transfer pass has been inserted this build.
        // FullscreenQuad passes cannot sample the multisample backbuffer directly;
        // the first FullscreenQuad that reads it triggers an automatic resolve.
        bool msaaResolvedThisFrame = false;

        for (const auto &passDesc : sortedPasses) {
            // Look up render callback from the Python callbacks map
            auto callbackIt = m_pythonCallbacks.find(passDesc.name);
            if (callbackIt == m_pythonCallbacks.end()) {
                continue;
            }
            auto callback = callbackIt->second;

            // Determine color targets (MRT support).
            // Build a map of slot → ResourceHandle for all declared color outputs.
            // Slot 0 defaults to the MSAA backbuffer if not specified.
            std::map<int, vk::ResourceHandle> colorTargets;
            for (const auto &[slot, texName] : passDesc.writeColors) {
                if (texName.empty()) {
                    continue;
                }
                auto texIt = texDescMap.find(texName);
                if (texIt != texDescMap.end() && texIt->second->isBackbuffer) {
                    colorTargets[slot] = m_importedColorTarget;
                } else {
                    // Non-backbuffer texture: look up pre-registered transient handle
                    auto rtIt = customRTHandles.find(texName);
                    if (rtIt != customRTHandles.end()) {
                        colorTargets[slot] = rtIt->second;
                    }
                }
            }
            // Default: if no color outputs declared and not a shadow pass,
            // write to MSAA backbuffer at slot 0.
            // Shadow passes are depth-only and should have no color attachments.
            bool isShadowPassAction = (passDesc.action == GraphPassActionType::DrawShadowCasters);
            if (colorTargets.empty() && !isShadowPassAction) {
                colorTargets[0] = m_importedColorTarget;
            }
            // Primary color target (slot 0) — used for MSAA resolve and compute fallback
            vk::ResourceHandle primaryColorTarget = colorTargets.count(0) ? colorTargets[0] : m_importedColorTarget;

            // Collect non-depth read texture handles for builder.Read()
            // This creates proper DAG edges and Vulkan barriers for
            // color texture dependencies between passes.
            std::vector<vk::ResourceHandle> colorReadHandles;
            bool readsDepth = false;
            for (const auto &readTex : passDesc.readTextures) {
                auto texIt = texDescMap.find(readTex);
                if (texIt != texDescMap.end()) {
                    if (texIt->second->isDepth) {
                        readsDepth = true;
                    } else if (!texIt->second->isBackbuffer) {
                        // Non-depth, non-backbuffer read: look up custom RT handle
                        auto rtIt = customRTHandles.find(readTex);
                        if (rtIt != customRTHandles.end()) {
                            colorReadHandles.push_back(rtIt->second);
                        }
                    }
                }
            }

            // Build input binding handles: map sampler name → ResourceHandle
            // so the execute lambda can resolve VkImageViews at runtime.
            std::vector<std::pair<std::string, vk::ResourceHandle>> inputBindingHandles;
            for (const auto &[samplerName, textureName] : passDesc.inputBindings) {
                auto rtIt = customRTHandles.find(textureName);
                if (rtIt != customRTHandles.end()) {
                    inputBindingHandles.emplace_back(samplerName, rtIt->second);
                } else {
                    INFLOG_WARN("SceneRenderGraph: Input binding '", samplerName, "' references unknown texture '",
                                textureName, "'");
                }
            }

            // Determine depth relationship
            bool writesDepth = !passDesc.writeDepth.empty();

            // Read clear values from the Python graph description, but
            // allow camera ClearFlags to override the first color-clearing pass.
            bool clearColor = passDesc.clearColor;
            bool clearDepth = passDesc.clearDepth;
            float clearColorR = passDesc.clearColorR;
            float clearColorG = passDesc.clearColorG;
            float clearColorB = passDesc.clearColorB;
            float clearColorA = passDesc.clearColorA;
            float clearDepthVal = passDesc.clearDepthValue;

            // Apply camera-driven clear overrides to the first pass that clears color.
            if (m_hasCameraClearOverride && passDesc.clearColor) {
                switch (m_cameraClearFlags) {
                case CameraClearFlags::Skybox:
                    clearColor = true;
                    clearDepth = true;
                    clearColorR = 0.0f;
                    clearColorG = 0.0f;
                    clearColorB = 0.0f;
                    clearColorA = 1.0f;
                    break;
                case CameraClearFlags::SolidColor:
                    clearColor = true;
                    clearDepth = true;
                    clearColorR = m_cameraBgColor.r;
                    clearColorG = m_cameraBgColor.g;
                    clearColorB = m_cameraBgColor.b;
                    clearColorA = m_cameraBgColor.a;
                    break;
                case CameraClearFlags::DepthOnly:
                    clearColor = false;
                    clearDepth = true;
                    break;
                case CameraClearFlags::DontClear:
                    clearColor = false;
                    clearDepth = false;
                    break;
                }
                // Record the pass name for per-frame clear-value updates
                // (Bug 3 / Bug 7 fix — Execute() uses this to call
                // UpdatePassClearColor() without rebuilding the graph).
                m_mainClearPassName = passDesc.name;
                // Only override the first eligible pass
                m_hasCameraClearOverride = false;
            }

            // Capture depth state for the lambda (by value — sharedDepth
            // is updated between iterations so we capture the CURRENT value)
            vk::ResourceHandle depthForThisPass = sharedDepth;
            bool needsCreateDepth = writesDepth && !sharedDepth.IsValid();
            bool passReadsDepth = readsDepth && !writesDepth;

            // MSAA resolve is performed explicitly after graph execution
            // (ResolveSceneMsaa) to keep ALL passes compatible with
            // m_internalRenderPass (which has no resolve attachment).
            // Using subpass resolve would add a resolve attachment to this
            // pass's VkRenderPass, making it incompatible with pipelines
            // created against m_internalRenderPass (attachment count mismatch).
            vk::ResourceHandle resolveTarget;

            // =================================================================
            // Compute passes use AddComputePass() — no render pass,
            // no color/depth attachments, no render area.
            // Respects Python-declared read/write resources for proper
            // DAG edges and Vulkan barriers.
            // =================================================================
            if (passDesc.action == GraphPassActionType::Compute) {
                // Collect all resource handles declared by Python.
                // Read-only textures (non-depth, non-backbuffer):
                std::vector<vk::ResourceHandle> computeReadHandles = colorReadHandles;
                // Write target: use primary (slot 0) color target
                vk::ResourceHandle computeWriteTarget = primaryColorTarget;

                m_renderGraph->AddComputePass(passDesc.name, [callback, computeReadHandles, computeWriteTarget, width,
                                                              height](vk::PassBuilder &builder) {
                    // Declare read dependencies for proper DAG edges
                    for (const auto &readHandle : computeReadHandles) {
                        builder.Read(readHandle, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
                    }
                    // Declare read/write access to the output target
                    builder.ReadWrite(computeWriteTarget, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

                    // Return execute callback
                    return [callback, width, height](vk::RenderContext &ctx) {
                        if (callback) {
                            callback(ctx, width, height);
                        }
                    };
                });
                continue;
            }

            // =================================================================
            // FullscreenQuad passes: fullscreen triangle with named shader,
            // push constants, and input texture sampling.
            // Uses FullscreenRenderer to manage pipeline cache + draw.
            //
            // MSAA handling:
            //   - Reading the MSAA backbuffer: a multisample image cannot be
            //     sampled by a regular sampler2D.  When MSAA is active, an
            //     automatic transfer pass resolves the backbuffer to the 1x
            //     resolve target before the first FullscreenQuad that reads
            //     it.  Subsequent reads reference the 1x resolve target.
            //   - Writing to the MSAA backbuffer: the pipeline sample count
            //     must match the render pass attachment. We propagate the
            //     actual MSAA sample count into the FullscreenPipelineKey.
            // =================================================================
            if (passDesc.action == GraphPassActionType::FullscreenQuad) {

                // ------ MSAA auto-resolve for backbuffer reads ------
                if (!msaaResolvedThisFrame && msaaSamples > VK_SAMPLE_COUNT_1_BIT &&
                    m_importedResolveTarget.IsValid()) {
                    bool readsBackbuffer = false;
                    for (const auto &readTex : passDesc.readTextures) {
                        auto texIt = texDescMap.find(readTex);
                        if (texIt != texDescMap.end() && texIt->second->isBackbuffer) {
                            readsBackbuffer = true;
                            break;
                        }
                    }
                    if (readsBackbuffer) {
                        // Insert a transfer pass that resolves MSAA → 1x.
                        // The render graph handles layout transitions via
                        // TransferRead / TransferWrite declarations.
                        auto importedColor = m_importedColorTarget;
                        auto importedResolve = m_importedResolveTarget;
                        VkImage msaaImage = m_sceneTarget->GetMsaaColorImage();
                        VkImage resolveImage = m_sceneTarget->GetColorImage();
                        uint32_t resolveW = width;
                        uint32_t resolveH = height;

                        m_renderGraph->AddTransferPass(
                            "__MSAA_resolve_pre_fs",
                            [importedColor, importedResolve, resolveW, resolveH, msaaImage,
                             resolveImage](vk::PassBuilder &builder) {
                                builder.TransferRead(importedColor);
                                builder.TransferWrite(importedResolve);
                                builder.SetRenderArea(resolveW, resolveH);

                                return [msaaImage, resolveImage, resolveW,
                                        resolveH](vk::RenderContext &ctx) {
                                    VkImageResolve region{};
                                    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                                    region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                                    region.extent = {resolveW, resolveH, 1};

                                    vkCmdResolveImage(ctx.GetCommandBuffer(), msaaImage,
                                                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, resolveImage,
                                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
                                };
                            });
                        msaaResolvedThisFrame = true;
                    }
                }

                // Capture references for the execute lambda
                FullscreenRenderer *fsRenderer = &m_fullscreenRenderer;
                vk::RenderGraph *renderGraphPtr = m_renderGraph.get();
                std::string capturedPassName = passDesc.name;
                std::string shaderName = passDesc.shaderName;
                auto pushConstantsVec = passDesc.pushConstants;

                // Input textures from readTextures.
                // For MSAA backbuffer reads, use the 1x resolve target instead
                // (resolved by the transfer pass above).
                std::vector<vk::ResourceHandle> fsReadHandles = colorReadHandles;
                for (const auto &readTex : passDesc.readTextures) {
                    auto texIt = texDescMap.find(readTex);
                    if (texIt != texDescMap.end() && texIt->second->isBackbuffer) {
                        if (msaaSamples > VK_SAMPLE_COUNT_1_BIT && m_importedResolveTarget.IsValid()) {
                            fsReadHandles.push_back(m_importedResolveTarget);
                        } else {
                            fsReadHandles.push_back(m_importedColorTarget);
                        }
                    }
                }

                // Determine output target (primary color)
                vk::ResourceHandle fsOutputTarget = primaryColorTarget;

                // Determine MSAA sample count and output format.
                // When writing to the MSAA backbuffer the pipeline sample
                // count must match the render pass attachment.
                VkSampleCountFlagBits fsSamples = VK_SAMPLE_COUNT_1_BIT;
                VkFormat fsColorFormat = m_sceneTarget->GetColorFormat();
                for (const auto &[slot, texName] : passDesc.writeColors) {
                    if (slot == 0 && !texName.empty()) {
                        auto texIt = texDescMap.find(texName);
                        if (texIt != texDescMap.end()) {
                            if (texIt->second->isBackbuffer && msaaSamples > VK_SAMPLE_COUNT_1_BIT) {
                                fsSamples = msaaSamples;
                            }
                            if (!texIt->second->isBackbuffer && texIt->second->format != VK_FORMAT_UNDEFINED) {
                                fsColorFormat = texIt->second->format;
                            }
                        }
                    }
                }

                // Determine pass dimensions (check output texture sizeDivisor)
                uint32_t fsPassWidth = width;
                uint32_t fsPassHeight = height;
                for (const auto &[slot, texName] : passDesc.writeColors) {
                    if (slot == 0 && !texName.empty()) {
                        auto texIt = texDescMap.find(texName);
                        if (texIt != texDescMap.end() && texIt->second->sizeDivisor > 1) {
                            fsPassWidth = std::max(1u, width / texIt->second->sizeDivisor);
                            fsPassHeight = std::max(1u, height / texIt->second->sizeDivisor);
                        }
                    }
                }

                m_renderGraph->AddPass(
                    passDesc.name, [=](vk::PassBuilder &builder) {
                        // Declare read dependencies for DAG edges + barriers
                        for (const auto &readHandle : fsReadHandles) {
                            builder.Read(readHandle);
                        }
                        // Declare color output
                        builder.WriteColor(fsOutputTarget, 0);
                        builder.SetRenderArea(fsPassWidth, fsPassHeight);

                        return [=](vk::RenderContext &ctx) {
                            // Get the VkRenderPass for pipeline creation (available post-Compile)
                            VkRenderPass rp = renderGraphPtr->GetPassRenderPass(capturedPassName);
                            if (rp == VK_NULL_HANDLE)
                                return;

                            // Resolve input texture views
                            std::vector<VkImageView> inputViews;
                            for (const auto &readHandle : fsReadHandles) {
                                VkImageView view = ctx.GetTexture(readHandle);
                                if (view != VK_NULL_HANDLE) {
                                    inputViews.push_back(view);
                                }
                            }

                            // Build pipeline key and ensure pipeline exists
                            FullscreenPipelineKey key;
                            key.shaderName = shaderName;
                            key.renderPass = rp;
                            key.samples = fsSamples;
                            key.colorFormat = fsColorFormat;
                            key.inputTextureCount = static_cast<uint32_t>(inputViews.size());

                            const auto &entry = fsRenderer->EnsurePipeline(key);
                            if (entry.pipeline == VK_NULL_HANDLE)
                                return;

                            // Allocate descriptor set for input textures
                            VkDescriptorSet descSet = fsRenderer->AllocateDescriptorSet(
                                entry.descSetLayout, inputViews, fsRenderer->GetLinearSampler());

                            // Pack push constants from Python graph description
                            FullscreenPushConstants pc{};
                            uint32_t pcSize = 0;
                            for (const auto &[name, value] : pushConstantsVec) {
                                if (pcSize / sizeof(float) < 32) {
                                    pc.values[pcSize / sizeof(float)] = value;
                                    pcSize += sizeof(float);
                                }
                            }

                            // Draw fullscreen triangle
                            fsRenderer->Draw(ctx.GetCommandBuffer(), entry, descSet, pc, pcSize, fsPassWidth,
                                             fsPassHeight);
                        };
                    });
                continue;
            }

            m_renderGraph->AddPass(passDesc.name, [=, &sharedDepth](vk::PassBuilder &builder) {
                // Local alias to make vkCore capturable by nested lambdas (MSVC C3481)
                InfVkCoreModular *localVkCore = vkCore;

                // ----- Determine pass dimensions -----
                // Shadow caster passes may use custom-sized depth textures.
                // Determine the actual pass dimensions from the depth target.
                uint32_t passWidth = width;
                uint32_t passHeight = height;
                bool isShadowPass = (passDesc.action == GraphPassActionType::DrawShadowCasters);

                // For shadow passes, look up the depth texture dimensions
                if (isShadowPass && !passDesc.writeDepth.empty()) {
                    auto depthTexIt = texDescMap.find(passDesc.writeDepth);
                    if (depthTexIt != texDescMap.end()) {
                        if (depthTexIt->second->width > 0)
                            passWidth = depthTexIt->second->width;
                        if (depthTexIt->second->height > 0)
                            passHeight = depthTexIt->second->height;
                    }
                }

                // ----- Depth -----
                vk::ResourceHandle depth;
                if (isShadowPass && !passDesc.writeDepth.empty()) {
                    // Shadow pass writes to a pre-registered custom-size depth texture
                    auto rtIt = customRTHandles.find(passDesc.writeDepth);
                    if (rtIt != customRTHandles.end()) {
                        depth = rtIt->second;
                        builder.WriteDepth(depth);
                    } else {
                        // Fallback: create inline
                        auto depthTexIt = texDescMap.find(passDesc.writeDepth);
                        VkFormat shadowDepthFmt =
                            depthTexIt != texDescMap.end() ? depthTexIt->second->format : VK_FORMAT_D32_SFLOAT;
                        depth = builder.CreateDepthStencil(passDesc.writeDepth, passWidth, passHeight, shadowDepthFmt,
                                                           VK_SAMPLE_COUNT_1_BIT);
                        builder.WriteDepth(depth);
                    }
                } else if (needsCreateDepth) {
                    // First pass that writes depth: create the shared resource
                    depth = builder.CreateDepthStencil("SceneDepth", width, height, depthFormat, msaaSamples);
                    builder.WriteDepth(depth);
                    // Store for subsequent passes (captured by ref)
                    sharedDepth = depth;
                } else if (writesDepth && depthForThisPass.IsValid()) {
                    // Later pass that also writes depth (rare)
                    builder.WriteDepth(depthForThisPass);
                } else if (passReadsDepth && depthForThisPass.IsValid()) {
                    // Pass reads depth (e.g., skybox, transparent) — attach as read-only
                    builder.ReadDepth(depthForThisPass);
                }

                // ----- Color reads (non-depth textures) -----
                // Declare Read() for each color texture this pass reads.
                // This creates proper DAG edges and Vulkan barriers.
                for (const auto &readHandle : colorReadHandles) {
                    builder.Read(readHandle);
                }

                // ----- Input binding reads (sampled textures, e.g. shadow map) -----
                // Input bindings reference textures by name for descriptor
                // binding at draw time. We also need Read() DAG edges here so that:
                //   1. The writer pass is not dead-pass-culled.
                //   2. Vulkan barriers transition the texture for shader read.
                for (const auto &[name, handle] : inputBindingHandles) {
                    builder.Read(handle);
                }

                // ----- Color outputs (MRT) -----
                // Write all declared color targets at their respective slots.
                for (const auto &[slot, handle] : colorTargets) {
                    builder.WriteColor(handle, slot);
                }

                // ----- MSAA Resolve (only on the last backbuffer pass) -----
                if (resolveTarget.IsValid()) {
                    builder.WriteResolve(resolveTarget);
                }

                // ----- Render area -----
                builder.SetRenderArea(passWidth, passHeight);

                // ----- Clear values -----
                if (clearColor) {
                    builder.SetClearColor(clearColorR, clearColorG, clearColorB, clearColorA);
                }
                if (clearDepth) {
                    builder.SetClearDepth(clearDepthVal, 0);
                }

                // Return execute callback.
                // For scene passes with "shadowMap" input, update per-graph
                // shadow descriptor set (set 1) for multi-camera isolation.
                VkDescriptorSet graphShadowDesc = m_perViewDescSet; // capture by value
                return [callback, passWidth, passHeight, inputBindingHandles, localVkCore, isShadowPass,
                        graphShadowDesc](vk::RenderContext &ctx) {
                    // Resolve input texture bindings → VkImageViews
                    if (!inputBindingHandles.empty()) {
                        for (const auto &[samplerName, resHandle] : inputBindingHandles) {
                            VkImageView view = ctx.GetTexture(resHandle);
                            if (view != VK_NULL_HANDLE) {
                                // "shadowMap" input binding →
                                // update this graph's per-view descriptor (set 1, binding 0)
                                if (samplerName == "shadowMap") {
                                    VkSampler shadowSampler = localVkCore->GetShadowDepthSampler();
                                    if (shadowSampler != VK_NULL_HANDLE && graphShadowDesc != VK_NULL_HANDLE) {
                                        localVkCore->UpdatePerViewShadowMap(graphShadowDesc, view, shadowSampler);
                                    }
                                }
                            }
                        }
                    }

                    if (callback) {
                        callback(ctx, passWidth, passHeight);
                    }
                };
            });
        }

        // ====================================================================
        // Auto-append _ComponentGizmos pass: draws queue 30000-32000
        // (Python-defined per-component gizmos) with depth testing against
        // existing scene geometry. Runs before editor gizmos.
        // ====================================================================
        {
            auto compGizmoCallbackIt = m_pythonCallbacks.find("_ComponentGizmos");
            if (compGizmoCallbackIt != m_pythonCallbacks.end()) {
                auto compGizmoCallback = compGizmoCallbackIt->second;
                vk::ResourceHandle compGizmoColorTarget = m_importedColorTarget;
                vk::ResourceHandle compGizmoDepthTarget = sharedDepth;

                m_renderGraph->AddPass("_ComponentGizmos", [=](vk::PassBuilder &builder) {
                    builder.WriteColor(compGizmoColorTarget, 0);
                    if (compGizmoDepthTarget.IsValid()) {
                        builder.ReadDepth(compGizmoDepthTarget);
                    }
                    builder.SetRenderArea(width, height);

                    return [compGizmoCallback, width, height](vk::RenderContext &ctx) {
                        if (compGizmoCallback) {
                            compGizmoCallback(ctx, width, height);
                        }
                    };
                });
            }
        }

        // ====================================================================
        // Auto-append _EditorGizmos pass: draws queue 32001-32500 (grid +
        // gizmos) into the MSAA backbuffer with depth testing against
        // existing scene geometry. No clear — additive over previous passes.
        // In game view no draw calls fall in this range, so the pass is a no-op.
        // ====================================================================
        {
            auto gizmoCallbackIt = m_pythonCallbacks.find("_EditorGizmos");
            if (gizmoCallbackIt != m_pythonCallbacks.end()) {
                auto gizmoCallback = gizmoCallbackIt->second;
                vk::ResourceHandle gizmoColorTarget = m_importedColorTarget;
                vk::ResourceHandle gizmoDepthTarget = sharedDepth;

                m_renderGraph->AddPass("_EditorGizmos", [=](vk::PassBuilder &builder) {
                    builder.WriteColor(gizmoColorTarget, 0);
                    if (gizmoDepthTarget.IsValid()) {
                        builder.ReadDepth(gizmoDepthTarget);
                    }
                    builder.SetRenderArea(width, height);

                    return [gizmoCallback, width, height](vk::RenderContext &ctx) {
                        if (gizmoCallback) {
                            gizmoCallback(ctx, width, height);
                        }
                    };
                });
            }
        }

        // ====================================================================
        // Auto-append _EditorTools pass: draws queue 32501-32700
        // (translate/rotate/scale handles) into the MSAA backbuffer.
        // No depth read — handles render on top of everything.
        // ====================================================================
        {
            auto toolsCallbackIt = m_pythonCallbacks.find("_EditorTools");
            if (toolsCallbackIt != m_pythonCallbacks.end()) {
                auto toolsCallback = toolsCallbackIt->second;
                vk::ResourceHandle toolsColorTarget = m_importedColorTarget;

                m_renderGraph->AddPass("_EditorTools", [=](vk::PassBuilder &builder) {
                    builder.WriteColor(toolsColorTarget, 0);
                    // No depth read — editor tools always render on top
                    builder.SetRenderArea(width, height);

                    return [toolsCallback, width, height](vk::RenderContext &ctx) {
                        if (toolsCallback) {
                            toolsCallback(ctx, width, height);
                        }
                    };
                });
            }
        }
    }

    // Set output for proper resource tracking and dead-pass culling.
    // If the Python graph output references the resolved backbuffer, use
    // m_importedResolveTarget so the culling algorithm traces through
    // the MSAA resolve and keeps all scene passes alive.
    bool outputSet = false;
    if (m_hasPythonGraph && !m_pythonGraphDesc.outputTexture.empty()) {
        auto texIt = std::find_if(m_pythonGraphDesc.textures.begin(), m_pythonGraphDesc.textures.end(),
                                  [&](const GraphTextureDesc &t) { return t.name == m_pythonGraphDesc.outputTexture; });
        if (texIt != m_pythonGraphDesc.textures.end()) {
            if (!texIt->isBackbuffer && !texIt->isDepth) {
                // Custom RT output — look up its handle
                auto rtIt = customRTHandles.find(m_pythonGraphDesc.outputTexture);
                if (rtIt != customRTHandles.end()) {
                    m_renderGraph->SetOutput(rtIt->second);
                    outputSet = true;
                }
            }
        }
    }
    if (!outputSet && m_importedColorTarget.IsValid()) {
        m_renderGraph->SetOutput(m_importedColorTarget);
    }

    m_graphBuilt = true;
}

} // namespace infengine
