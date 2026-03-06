/**
 * @file SceneRenderGraph.h
 * @brief RenderGraph-based scene rendering system
 *
 * This class fully integrates with the low-level vk::RenderGraph API for
 * declarative, frame-graph-driven rendering. All rendering is now handled
 * via RenderGraph passes - no more imperative BeginRenderPass/EndRenderPass.
 *
 * Architecture:
 * - Uses vk::RenderGraph for automatic resource management and barrier handling
 * - All passes are defined via RenderGraph's AddPass API
 * - Transient resources managed by RenderGraph
 * - External resources (scene target) imported into RenderGraph
 * - Supports GPU->CPU readback for Python/ML integration
 */

#pragma once

#include "FullscreenRenderer.h"
#include "InfRenderStruct.h"
#include "RenderGraphDescription.h"
#include "RenderPassOutput.h"
#include "vk/RenderGraph.h"
#include "vk/VkDeviceContext.h"
#include "vk/VkPipelineManager.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace infengine
{

class InfVkCoreModular;
class InfMaterial;
class InfScreenUIRenderer;
class SceneRenderTarget;

// Forward-declare from Camera.h
enum class CameraClearFlags;

/**
 * @brief Pass type enumeration for scene rendering
 */
enum class ScenePassType
{
    DepthPrePass, ///< Depth-only pass for early-z optimization
    ShadowPass,   ///< Shadow map generation
    MainColor,    ///< Main color pass with materials
    Transparent,  ///< Transparent objects (back-to-front)
    UI,           ///< UI overlay (ImGui)
    Custom        ///< Custom user-defined pass
};

/**
 * @brief Configuration for a scene render pass
 */
struct ScenePassConfig
{
    std::string name;
    ScenePassType type = ScenePassType::MainColor;
    bool enabled = true;

    // Clear settings
    bool clearColor = true;
    bool clearDepth = true;
    float clearColorValue[4] = {0.1f, 0.1f, 0.1f, 1.0f};
    float clearDepthValue = 1.0f;

    // Output settings for readback
    bool hasOwnRenderTarget = false; ///< If true, creates dedicated render target for this pass
    bool enableReadback = false;     ///< If true, allows CPU readback of output

    // Input dependencies (pass names to read from)
    std::vector<std::string> inputPasses;

    // ========================================================================
    // Phase 0: Resource and Subpass Support (NEW)
    // ========================================================================

    // Resource declarations (if empty, uses default scene target)
    std::vector<vk::ResourceHandle> inputTextures;  ///< Input textures to read from
    std::vector<vk::ResourceHandle> outputTextures; ///< Output color attachments
    vk::ResourceHandle depthOutput;                 ///< Depth attachment (optional)

    // Subpass support - allows multiple subpasses in one RenderPass
    bool isSubpass = false;     ///< If true, this is a subpass of a parent pass
    std::string parentPassName; ///< Parent pass name (if isSubpass == true)
    uint32_t subpassIndex = 0;  ///< Index within parent pass's subpasses
};

/**
 * @brief Pass render callback signature using RenderGraph context
 * @param ctx RenderGraph context for drawing commands
 * @param width Render target width
 * @param height Render target height
 */
using ScenePassRenderCallback = std::function<void(vk::RenderContext &ctx, uint32_t width, uint32_t height)>;

/**
 * @brief RenderGraph-based scene rendering system
 *
 * Provides a fully declarative rendering pipeline using vk::RenderGraph.
 * All rendering is handled through RenderGraph passes with automatic
 * resource management and barrier handling.
 */
class SceneRenderGraph
{
  public:
    SceneRenderGraph();
    ~SceneRenderGraph();

    // Non-copyable
    SceneRenderGraph(const SceneRenderGraph &) = delete;
    SceneRenderGraph &operator=(const SceneRenderGraph &) = delete;

    /**
     * @brief Initialize the scene render graph
     * @param vkCore Vulkan core for resource access
     * @param sceneTarget Scene render target for external resources
     * @return true if successful
     */
    bool Initialize(InfVkCoreModular *vkCore, SceneRenderTarget *sceneTarget);

    /**
     * @brief Cleanup resources
     */
    void Destroy();

    // ========================================================================
    // Phase 2: Python-Driven RenderGraph Topology
    // ========================================================================

    /**
     * @brief Apply a Python-defined render graph topology
     *
     * Receives a RenderGraphDescription from Python and translates it into
     * SceneRenderGraph passes with appropriate callbacks. C++ retains
     * compilation authority (DAG compilation, barrier insertion, resource
     * allocation) while Python has definition authority (topology, pass
     * order, resource connections).
     *
     * @param desc The graph topology description from Python
     */
    void ApplyPythonGraph(const RenderGraphDescription &desc);

    /**
     * @brief Set the screen UI renderer for DrawScreenUI passes
     * @param renderer Pointer to the screen UI renderer (may be nullptr)
     */
    void SetScreenUIRenderer(InfScreenUIRenderer *renderer)
    {
        m_screenUIRenderer = renderer;
    }

    /**
     * @brief Check if a Python graph topology has been applied
     */
    [[nodiscard]] bool HasPythonGraph() const
    {
        return m_hasPythonGraph;
    }

    /**
     * @brief Get the MSAA sample count requested by the current Python graph (0 = no preference).
     */
    [[nodiscard]] int GetRequestedMsaaSamples() const
    {
        return m_hasPythonGraph ? m_pythonGraphDesc.msaaSamples : 0;
    }

    // ========================================================================
    // Resource Management (Phase 0 - NEW)
    // ========================================================================

    /**
     * @brief Create a transient texture resource
     * @param name Resource name for debugging
     * @param width Texture width
     * @param height Texture height
     * @param format Vulkan format
     * @param isTransient If true, resource can be aliased
     * @return Resource handle for use in pass configuration
     */
    vk::ResourceHandle CreateTransientTexture(const std::string &name, uint32_t width, uint32_t height, VkFormat format,
                                              bool isTransient = true);

    /**
     * @brief Get the scene color target resource handle
     * @return Handle to the imported scene color target
     */
    [[nodiscard]] vk::ResourceHandle GetSceneColorTarget() const
    {
        return m_importedColorTarget;
    }

    /**
     * @brief Get the scene depth target resource handle
     * @return Handle to the imported scene depth target
     */
    [[nodiscard]] vk::ResourceHandle GetSceneDepthTarget() const
    {
        return m_importedDepthTarget;
    }

    // ========================================================================
    // Execution (Pure RenderGraph)
    // ========================================================================

    /**
     * @brief Build and execute the render graph for the current frame
     * @param commandBuffer Command buffer to record into
     *
     * This method:
     * 1. Rebuilds the RenderGraph if needed
     * 2. Calls RenderGraph::Compile() to optimize passes and allocate resources
     * 3. Applies per-frame camera clear overrides without rebuild
     * 4. Calls RenderGraph::Execute() to record all commands
     */
    void Execute(VkCommandBuffer commandBuffer);

    /**
     * @brief Called when scene render target is resized
     */
    void OnResize(uint32_t width, uint32_t height);

    /**
     * @brief Force rebuild of the render graph on next frame
     */
    void MarkDirty()
    {
        m_needsRebuild = true;
    }

    void UpdateMainPassClearSettings(CameraClearFlags clearFlags, const glm::vec4 &bgColor);

    // ========================================================================
    // Pass Output Access (for Python/ML integration)
    // ========================================================================

    /**
     * @brief Get list of pass names that have readback enabled
     */
    [[nodiscard]] std::vector<std::string> GetReadablePassNames() const;

    /**
     * @brief Read color pixels from a pass output
     * @param passName Name of the pass to read from
     * @param outData Output vector for RGBA8 pixel data
     * @return true if successful
     */
    bool ReadPassColorPixels(const std::string &passName, std::vector<uint8_t> &outData);

    /**
     * @brief Read depth pixels from a pass output
     * @param passName Name of the pass to read from
     * @param outData Output vector for float32 depth data
     * @return true if successful
     */
    bool ReadPassDepthPixels(const std::string &passName, std::vector<float> &outData);

    /**
     * @brief Get pass output dimensions
     * @param passName Name of the pass
     * @param outWidth Output width
     * @param outHeight Output height
     * @return true if pass exists
     */
    bool GetPassOutputSize(const std::string &passName, uint32_t &outWidth, uint32_t &outHeight) const;

    /**
     * @brief Get pass output texture ID for ImGui display
     * @param passName Name of the pass
     * @return Texture ID or 0 if not available
     */
    [[nodiscard]] uint64_t GetPassTextureId(const std::string &passName) const;

    // ========================================================================
    // Debug
    // ========================================================================

    /**
     * @brief Get debug visualization of the render graph
     */
    [[nodiscard]] std::string GetDebugString() const;

    /**
     * @brief Get pass count
     */
    [[nodiscard]] size_t GetPassCount() const
    {
        return m_hasPythonGraph ? m_pythonGraphDesc.passes.size() : 0;
    }

    // ========================================================================
    // Per-Graph Draw Call Cache (for multi-camera rendering)
    // ========================================================================

    /// @brief Cache draw calls for this render graph (called by SubmitCulling)
    void SetCachedDrawCalls(std::vector<DrawCall> drawCalls)
    {
        m_cachedDrawCalls = std::move(drawCalls);
        m_hasCachedDrawCalls = true;
    }

    /// @brief Get cached draw calls
    [[nodiscard]] const std::vector<DrawCall> &GetCachedDrawCalls() const
    {
        return m_cachedDrawCalls;
    }

    /// @brief Check if this graph has cached draw calls
    [[nodiscard]] bool HasCachedDrawCalls() const
    {
        return m_hasCachedDrawCalls;
    }

    // ========================================================================
    // Per-Graph Camera VP Cache (for multi-camera UBO updates)
    // ========================================================================

    /// @brief Cache camera VP matrices (called by SubmitCulling)
    void SetCachedCameraVP(const glm::mat4 &view, const glm::mat4 &proj)
    {
        m_cachedView = view;
        m_cachedProj = proj;
        m_hasCachedCameraVP = true;
    }

    /// @brief Check if this graph has cached camera VP matrices
    [[nodiscard]] bool HasCachedCameraVP() const
    {
        return m_hasCachedCameraVP;
    }

    /// @brief Get cached view matrix
    [[nodiscard]] const glm::mat4 &GetCachedView() const
    {
        return m_cachedView;
    }

    /// @brief Get cached projection matrix
    [[nodiscard]] const glm::mat4 &GetCachedProj() const
    {
        return m_cachedProj;
    }

    /// @brief Get per-graph shadow descriptor set (set 1)
    [[nodiscard]] VkDescriptorSet GetPerViewDescriptorSet() const
    {
        return m_perViewDescSet;
    }

    /**
     * @brief Explicit MSAA resolve from 4x MSAA color to 1x color target
     *
     * Called after all render graph passes finish, so every draw call
     * (scene objects, gizmos, grid) benefits from MSAA.
     */
    void ResolveSceneMsaa(VkCommandBuffer commandBuffer);

  private:
    /**
     * @brief Build the vk::RenderGraph from configured passes
     */
    void BuildRenderGraph();

    /**
     * @brief Import scene target resources into RenderGraph
     */
    void ImportSceneTargetResources();

    InfVkCoreModular *m_vkCore = nullptr;
    SceneRenderTarget *m_sceneTarget = nullptr;
    InfScreenUIRenderer *m_screenUIRenderer = nullptr;

    // Build state
    bool m_needsRebuild = true;
    bool m_needsCompile = true;
    bool m_graphBuilt = false;
    bool m_hasPythonGraph = false;

    // Python graph description (stored for BuildRenderGraph)
    RenderGraphDescription m_pythonGraphDesc;

    // Python-driven render callbacks: pass name → ScenePassRenderCallback.
    // Populated by ApplyPythonGraph(). BuildRenderGraph() reads this map directly,
    // bypassing the intermediate ScenePassConfig conversion.
    std::unordered_map<std::string, ScenePassRenderCallback> m_pythonCallbacks;

    // The underlying render graph (now fully utilized)
    std::unique_ptr<vk::RenderGraph> m_renderGraph;

    // Imported resource handles from scene target
    vk::ResourceHandle m_importedColorTarget;
    vk::ResourceHandle m_importedResolveTarget; // 1x resolve target for MSAA
    vk::ResourceHandle m_importedDepthTarget;

    // Transient resources created by CreateTransientTexture()
    std::unordered_map<std::string, vk::ResourceHandle> m_transientResources;

    // Camera-driven clear overrides (set per-frame by UpdateMainPassClearSettings)
    bool m_hasCameraClearOverride = false;
    CameraClearFlags m_cameraClearFlags = {};
    glm::vec4 m_cameraBgColor{0.1f, 0.1f, 0.1f, 1.0f};

    // Previous frame's camera clear state — used to detect changes that
    // actually require a graph rebuild (= loadOp change) vs. changes that
    // only require updating clear *values* (no rebuild needed).
    bool m_prevClearStateValid = false;
    CameraClearFlags m_prevCameraClearFlags = {};
    glm::vec4 m_prevCameraBgColor{0.1f, 0.1f, 0.1f, 1.0f};

    // Name of the first graph pass that clears color (set during BuildRenderGraph).
    // Used to apply per-frame clear value updates without rebuilding the graph.
    std::string m_mainClearPassName;

    // Dimensions
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    // Per-graph draw call cache for multi-camera rendering
    std::vector<DrawCall> m_cachedDrawCalls;
    bool m_hasCachedDrawCalls = false;

    // Per-graph camera VP cache — set by SubmitCulling so the executor
    // uses the exact same matrices that were active during SetupCameraProperties.
    glm::mat4 m_cachedView{1.0f};
    glm::mat4 m_cachedProj{1.0f};
    bool m_hasCachedCameraVP = false;

    // Per-graph shadow descriptor set (set 1) — multi-camera shadow isolation.
    // Each graph owns its own descriptor set so different cameras sample
    // their own shadow map without vkUpdateDescriptorSets host-side races.
    VkDescriptorSet m_perViewDescSet = VK_NULL_HANDLE;

    // Fullscreen effect renderer — manages pipeline cache, descriptor pool,
    // and linear sampler for FullscreenQuad graph passes.
    FullscreenRenderer m_fullscreenRenderer;
};

} // namespace infengine
