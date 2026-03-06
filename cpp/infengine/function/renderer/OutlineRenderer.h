/**
 * @file OutlineRenderer.h
 * @brief Post-process selection outline renderer (Blender/Unity style)
 *
 * Extracted from InfVkCoreModular as part of the Phase 1 refactoring
 * (editor logic separation). This class owns all Vulkan resources
 * related to the screen-space selection outline:
 *   - Mask render pass / framebuffer / pipeline  (renders selected object as white silhouette)
 *   - Composite render pass / framebuffer / pipeline (edge detection + alpha blend on scene color)
 *
 * Lifecycle is managed by InfRenderer; the actual Vulkan command recording
 * is injected into the frame via InfVkCoreModular::SetPostSceneRenderCallback().
 */

#pragma once

#include "InfRenderStruct.h"
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>
#include <vulkan/vulkan.h>

namespace infengine
{

// Forward declarations
class InfVkCoreModular;
class SceneRenderTarget;

/**
 * @brief Self-contained post-process selection outline renderer.
 *
 * Usage:
 *   1. Initialize() after InfVkCoreModular + SceneRenderTarget are ready
 *   2. SetOutlineObjectId() each frame from InfRenderer
 *   3. RecordCommands() is called inside the post-scene-render callback
 *   4. OnResize() whenever the scene render target is resized
 *   5. Cleanup() or destructor releases all Vulkan resources
 */
class OutlineRenderer
{
  public:
    OutlineRenderer() = default;
    ~OutlineRenderer();

    // Non-copyable
    OutlineRenderer(const OutlineRenderer &) = delete;
    OutlineRenderer &operator=(const OutlineRenderer &) = delete;

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// @brief Initialize outline Vulkan resources.
    /// @param core Pointer to the Vulkan core (for device, shaders, UBO access)
    /// @param sceneTarget Pointer to the scene render target (mask image + scene color)
    /// @return true if initialization succeeded, false otherwise
    bool Initialize(InfVkCoreModular *core, SceneRenderTarget *sceneTarget);

    /// @brief Release all Vulkan resources.
    void Cleanup();

    /// @brief Recreate framebuffers after scene render target resize.
    void OnResize(uint32_t width, uint32_t height);

    /// @brief Check if outline resources are ready for rendering.
    [[nodiscard]] bool IsReady() const
    {
        return m_resourcesReady;
    }

    // ========================================================================
    // State
    // ========================================================================

    /// @brief Set the object ID to outline (0 = no outline).
    void SetOutlineObjectId(uint64_t objectId)
    {
        m_outlineObjectId = objectId;
    }

    /// @brief Get the current outline object ID.
    [[nodiscard]] uint64_t GetOutlineObjectId() const
    {
        return m_outlineObjectId;
    }

    /// @brief Check if there is an active outline to render.
    [[nodiscard]] bool HasActiveOutline() const
    {
        return m_outlineObjectId != 0;
    }

    /// @brief Set outline color (default bright orange).
    void SetOutlineColor(float r, float g, float b, float a)
    {
        m_outlineColor = glm::vec4(r, g, b, a);
    }

    /// @brief Set outline color from vec4.
    void SetOutlineColor(const glm::vec4 &color)
    {
        m_outlineColor = color;
    }

    /// @brief Set outline width in pixels (default 3.0).
    void SetOutlinePixelWidth(float width)
    {
        m_outlinePixelWidth = width;
    }

    // ========================================================================
    // Rendering
    // ========================================================================

    /// @brief Record outline mask + composite commands into the given command buffer.
    ///
    /// This renders the full outline pipeline:
    ///   1. Mask pass — renders selected object as white silhouette
    ///   2. Composite pass — edge detection on mask, alpha-blend onto scene color
    ///
    /// If no outline is active (objectId == 0) or resources aren't ready, this is a no-op.
    ///
    /// @param cmdBuf Active Vulkan command buffer
    /// @param drawCalls Current frame's draw calls (to find the selected object)
    void RecordCommands(VkCommandBuffer cmdBuf, const std::vector<DrawCall> &drawCalls);

    /// @brief Record the scene-color → SHADER_READ_ONLY transition barrier.
    ///
    /// When no outline is active, the scene color image still needs to be
    /// transitioned to SHADER_READ_ONLY_OPTIMAL for ImGui sampling.
    /// Call this from the post-scene-render callback when HasActiveOutline() is false.
    void RecordNoOutlineBarrier(VkCommandBuffer cmdBuf);

  private:
    // ========================================================================
    // Internal Vulkan Resource Creation
    // ========================================================================

    void CreateOutlineMaskRenderPass();
    void CreateOutlineCompositeRenderPass();
    void CreateOutlineFramebuffers();
    void CreateOutlineDescriptorResources();
    void CreateOutlinePipelines();

    // ========================================================================
    // Internal Rendering
    // ========================================================================

    void RenderOutlineMask(VkCommandBuffer cmdBuf, const std::vector<DrawCall> &drawCalls);
    void RenderOutlineComposite(VkCommandBuffer cmdBuf);

    // ========================================================================
    // References (non-owning)
    // ========================================================================

    InfVkCoreModular *m_core = nullptr;
    SceneRenderTarget *m_sceneRenderTarget = nullptr;

    // ========================================================================
    // Vulkan Resources (owned)
    // ========================================================================

    // Render passes
    VkRenderPass m_outlineMaskRenderPass = VK_NULL_HANDLE;
    VkRenderPass m_outlineCompositeRenderPass = VK_NULL_HANDLE;

    // Framebuffers
    VkFramebuffer m_outlineMaskFramebuffer = VK_NULL_HANDLE;
    VkFramebuffer m_outlineCompositeFramebuffer = VK_NULL_HANDLE;

    // Mask pipeline (renders selected object as white silhouette)
    VkPipeline m_outlineMaskPipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_outlineMaskPipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_outlineMaskDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_outlineMaskDescSet = VK_NULL_HANDLE;

    // Composite pipeline (fullscreen edge detection + blend)
    VkPipeline m_outlineCompositePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_outlineCompositePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_outlineCompositeDescSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_outlineCompositeDescSet = VK_NULL_HANDLE;

    // Shared descriptor pool for outline
    VkDescriptorPool m_outlineDescPool = VK_NULL_HANDLE;

    // ========================================================================
    // Outline Parameters
    // ========================================================================

    uint64_t m_outlineObjectId = 0;
    glm::vec4 m_outlineColor{1.0f, 0.5f, 0.0f, 1.0f}; // Bright orange
    float m_outlinePixelWidth = 3.0f;
    bool m_resourcesReady = false;
};

} // namespace infengine
