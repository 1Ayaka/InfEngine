#pragma once

#include "InfRenderStruct.h"
#include "RenderGraphDescription.h"
#include <function/scene/Camera.h>
#include <function/scene/PrimitiveMeshes.h>
#include <function/scene/SceneSystem.h>

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace infengine
{

// Forward declarations
class InfVkCoreModular;
class SceneRenderGraph;
class EditorGizmos;
class EditorTools;
class GizmosDrawCallBuffer;
class InfMaterial;
class CommandBuffer;
class TransientResourcePool;
struct RenderTargetHandle;

// ============================================================================
// CullingResults
// ============================================================================

/**
 * @brief Result of scene culling — visible draw calls ready for filtering.
 *
 * Returned by ScriptableRenderContext::Cull(). Contains all visible
 * draw calls that can then be filtered/sorted by DrawRenderers().
 */
struct CullingResults
{
    std::vector<DrawCall> drawCalls; ///< All visible draw calls (unfiltered)

    [[nodiscard]] size_t visibleObjectCount() const
    {
        return drawCalls.size();
    }
    [[nodiscard]] size_t visibleLightCount() const
    {
        return 0;
    } // TODO: light culling
};

// ============================================================================
// Editor Gizmos Context (internal — not exposed to Python)
// ============================================================================

/**
 * @brief Internal context for auto-appending editor gizmos after Submit().
 *
 * The SRC auto-appends gizmo draw calls so Python RenderPipeline
 * doesn't need to worry about editor-only rendering.
 */
struct EditorGizmosContext
{
    EditorGizmos *gizmos = nullptr;
    EditorTools *editorTools = nullptr;
    GizmosDrawCallBuffer *componentGizmos = nullptr; ///< Python-driven component gizmos
    std::shared_ptr<InfMaterial> gizmoMaterial;
    std::shared_ptr<InfMaterial> gridMaterial;
    std::shared_ptr<InfMaterial> editorToolsMaterial;
    std::shared_ptr<InfMaterial> componentGizmosMaterial;    ///< Material for component gizmos (queue 30000)
    std::shared_ptr<InfMaterial> componentGizmoIconMaterial; ///< Material for icon billboards (queue 31000)
    uint64_t selectedObjectId = 0;
    Scene *activeScene = nullptr;
    glm::vec3 cameraPos{0.0f};
};

// ============================================================================
// ScriptableRenderContext
// ============================================================================

/**
 * @brief Unity SRP-style render context.
 *
 * Provides the interface Python RenderPipeline uses to define rendering:
 *   1. SetupCameraProperties() — bind camera VP matrices
 *   2. Cull() — get visible objects
 *   3. ApplyGraph() — apply a Python-defined RenderGraph topology
 *   4. SubmitCulling() — upload all draw calls and execute graph passes
 *
 * Python calls happen once per frame; all heavy work (sorting, GPU uploads)
 * is in C++.
 */
class ScriptableRenderContext
{
  public:
    ScriptableRenderContext(InfVkCoreModular *vkCore, SceneRenderGraph *graph,
                            const EditorGizmosContext &gizmoCtx = {});

    /// @brief Set camera VP matrices for rendering
    void SetupCameraProperties(Camera *camera);

    /// @brief Cull scene objects visible to the given camera. Returns culling results.
    CullingResults Cull(Camera *camera);

    // ====================================================================
    // RenderGraph-driven API (replaces DrawRenderers + Submit combo)
    // ====================================================================

    /// @brief Apply a Python-defined RenderGraph topology.
    /// This sets the pass topology on the underlying SceneRenderGraph.
    /// Must be called before Submit() or SubmitCulling().
    void ApplyGraph(const RenderGraphDescription &desc);

    /// @brief Submit all culling results as full draw calls + execute graph.
    /// Replaces the DrawRenderers() + DrawSkybox() + Submit() combo.
    /// DrawCall filtering is done by RenderGraph pass callbacks.
    void SubmitCulling(const CullingResults &culling);

    // ====================================================================
    // Phase 2: CommandBuffer Integration
    // ====================================================================

    /// @brief Execute a deferred CommandBuffer.
    /// Commands are buffered and actually executed during Submit().
    void ExecuteCommandBuffer(CommandBuffer &cmd);

    // ====================================================================
    // Phase 2: Render Target Operations
    // ====================================================================

    /// @brief Get a handle representing the final camera render target.
    RenderTargetHandle GetCameraTarget(Camera *camera) const;

    // ====================================================================
    // Phase 2: Global Shader Parameters (immediate mode)
    // ====================================================================

    void SetGlobalTexture(const std::string &name, RenderTargetHandle handle);
    void SetGlobalFloat(const std::string &name, float value);
    void SetGlobalVector(const std::string &name, float x, float y, float z, float w);

    // ====================================================================
    // Scene access (RenderStack integration)
    // ====================================================================

    /// @brief Get the scene associated with this render context.
    /// Returns the gizmo context's active scene, or falls back to
    /// SceneManager::GetActiveScene().
    [[nodiscard]] Scene *GetScene() const
    {
        return m_scene;
    }

    // ====================================================================
    // Phase 2: TransientResourcePool injection
    // ====================================================================

    /// @brief Set the transient resource pool (called by InfRenderer during setup).
    void SetTransientResourcePool(TransientResourcePool *pool)
    {
        m_transientPool = pool;
    }

    /// @brief Get the transient resource pool.
    [[nodiscard]] TransientResourcePool *GetTransientResourcePool() const
    {
        return m_transientPool;
    }

    // ====================================================================
    // Phase 2: Global parameter accessors (for CommandBuffer execution)
    // ====================================================================

    [[nodiscard]] const std::unordered_map<std::string, float> &GetGlobalFloats() const
    {
        return m_globalFloats;
    }
    [[nodiscard]] const std::unordered_map<std::string, std::array<float, 4>> &GetGlobalVectors() const
    {
        return m_globalVectors;
    }
    [[nodiscard]] const std::unordered_map<std::string, uint32_t> &GetGlobalTextures() const
    {
        return m_globalTextures;
    }

  private:
    InfVkCoreModular *m_vkCore;
    SceneRenderGraph *m_graph;
    EditorGizmosContext m_gizmoCtx;

    // Draw calls accumulated by DrawRenderers() (in submission order)
    std::vector<DrawCall> m_orderedDrawCalls;

    Scene *m_scene = nullptr;
    Camera *m_activeCamera = nullptr;
    glm::mat4 m_cachedView{1.0f};
    glm::mat4 m_cachedProj{1.0f};
    bool m_hasCullData = false;
    bool m_submitted = false;

    // Phase 2: CommandBuffer deferred execution
    TransientResourcePool *m_transientPool = nullptr;
    std::vector<CommandBuffer *> m_pendingCommandBuffers;

    // Phase 2: Global shader parameter state
    std::unordered_map<std::string, float> m_globalFloats;
    std::unordered_map<std::string, std::array<float, 4>> m_globalVectors;
    std::unordered_map<std::string, uint32_t> m_globalTextures; // name → RT handle

    // Phase 2: Handle → pool slot mapping for transient RT resolution
    std::unordered_map<uint32_t, uint32_t> m_handleToSlotMap; // RenderTargetHandle.id → pool slot

    /// @brief Process all pending CommandBuffers' commands (RT management, globals).
    void ProcessPendingCommandBuffers();
};

// ============================================================================
// RenderPipelineCallback — C++ interface for Python render pipelines
// ============================================================================

/**
 * @brief Abstract base for render pipelines.
 *
 * Python classes override Render() via pybind11 trampoline.
 * The engine calls Render() once per frame with a ScriptableRenderContext
 * and the list of active cameras.
 */
class RenderPipelineCallback
{
  public:
    virtual ~RenderPipelineCallback() = default;

    /// @brief Called once per frame to define the rendering pass sequence.
    virtual void Render(ScriptableRenderContext &context, const std::vector<Camera *> &cameras) = 0;

    /// @brief Called when the pipeline is being replaced or engine is shutting down.
    virtual void Dispose()
    {
    }
};

} // namespace infengine
