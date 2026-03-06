/**
 * @file FullscreenRenderer.h
 * @brief Utility for drawing fullscreen triangles with named shaders
 *
 * Provides a lazy-initialised pipeline cache + descriptor management for
 * the FullscreenQuad graph pass action.  The OutlineRenderer already
 * establishes the "procedural fullscreen triangle" pattern; this class
 * generalises it for arbitrary shader pairs from the Python graph.
 *
 * Pipeline key: (shaderName, VkRenderPass, sampleCount, colorFormat,
 *                inputTextureCount).
 * Pipelines are created lazily on first use and cached for the lifetime
 * of the FullscreenRenderer.
 */

#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace infengine
{

class InfVkCoreModular;

namespace vk
{
class VkPipelineManager;
class VkDeviceContext;
} // namespace vk

// ============================================================================
// Push-constant layout for fullscreen effects (max 128 bytes / 32 floats)
// ============================================================================

/// Fixed-size push constant block shared by all fullscreen effect shaders.
/// Shaders define their own push_constant layout using a subset of these slots.
/// Up to 32 floats = 128 bytes (Vulkan guaranteed minimum).
struct FullscreenPushConstants
{
    float values[32] = {};
};

// ============================================================================
// Pipeline cache key
// ============================================================================

struct FullscreenPipelineKey
{
    std::string shaderName;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    uint32_t inputTextureCount = 0;

    bool operator==(const FullscreenPipelineKey &other) const
    {
        return shaderName == other.shaderName && renderPass == other.renderPass && samples == other.samples &&
               colorFormat == other.colorFormat && inputTextureCount == other.inputTextureCount;
    }
};

struct FullscreenPipelineKeyHash
{
    size_t operator()(const FullscreenPipelineKey &k) const
    {
        size_t h = std::hash<std::string>{}(k.shaderName);
        h ^= std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(k.renderPass)) << 1;
        h ^= std::hash<int>{}(static_cast<int>(k.samples)) << 2;
        h ^= std::hash<int>{}(static_cast<int>(k.colorFormat)) << 3;
        h ^= std::hash<uint32_t>{}(k.inputTextureCount) << 4;
        return h;
    }
};

// ============================================================================
// Cached pipeline entry
// ============================================================================

struct FullscreenPipelineEntry
{
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descSetLayout = VK_NULL_HANDLE;
    bool layoutOwned = false;
};

// ============================================================================
// FullscreenRenderer
// ============================================================================

/**
 * @brief Manages fullscreen-triangle pipeline creation and drawing
 *
 * Lifecycle:
 *   1. Initialize() with vkCore reference
 *   2. For each FullscreenQuad pass during BuildRenderGraph, call
 *      EnsurePipeline() to lazily create/cache the pipeline.
 *   3. During Execute, call Draw() with the appropriate parameters.
 *   4. Destroy() on shutdown.
 */
class FullscreenRenderer
{
  public:
    FullscreenRenderer() = default;
    ~FullscreenRenderer();

    /// Initialise with engine core references
    void Initialize(InfVkCoreModular *vkCore);

    /// Destroy all cached pipelines and resources
    void Destroy();

    /// Ensure a pipeline exists for the given key, creating it lazily if needed.
    /// Returns the cached entry (pipeline + layout + desc set layout).
    const FullscreenPipelineEntry &EnsurePipeline(const FullscreenPipelineKey &key);

    /// Allocate (or reuse) a descriptor set for the given layout + input textures.
    /// The returned set is valid for the current frame only.
    VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout, const std::vector<VkImageView> &inputViews,
                                          VkSampler sampler);

    /// Draw a fullscreen triangle.
    ///   - Binds the pipeline
    ///   - Binds descriptor set for input textures
    ///   - Pushes constants
    ///   - Draws 3 vertices
    void Draw(VkCommandBuffer cmdBuf, const FullscreenPipelineEntry &entry, VkDescriptorSet descSet,
              const FullscreenPushConstants &pushConstants, uint32_t pushConstantSize, uint32_t width, uint32_t height);

    /// Reset descriptor pool for a new frame (frees all previously allocated sets)
    void ResetPool();

    /// Get a linear sampler suitable for fullscreen texture sampling
    [[nodiscard]] VkSampler GetLinearSampler() const
    {
        return m_linearSampler;
    }

  private:
    InfVkCoreModular *m_vkCore = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    VkSampler m_linearSampler = VK_NULL_HANDLE;

    /// Pipeline cache
    std::unordered_map<FullscreenPipelineKey, FullscreenPipelineEntry, FullscreenPipelineKeyHash> m_pipelineCache;

    /// Descriptor pool for fullscreen effect sampling
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    /// Create a new pipeline for the given key
    FullscreenPipelineEntry CreatePipeline(const FullscreenPipelineKey &key);

    /// Create descriptor pool
    void CreateDescriptorPool();

    /// Create linear sampler
    void CreateLinearSampler();
};

} // namespace infengine
