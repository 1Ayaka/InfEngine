/**
 * @file RenderPassOutput.h
 * @brief Manages render pass output textures with GPU->CPU readback support
 *
 * This class handles:
 * - Output texture creation with transfer support
 * - Staging buffer for GPU->CPU data transfer
 * - Pixel data readback to host memory
 */

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace infengine
{

class InfVkCoreModular;

/**
 * @brief Manages output texture for a render pass with readback capability
 */
class RenderPassOutput
{
  public:
    RenderPassOutput(InfVkCoreModular *vkCore);
    ~RenderPassOutput();

    RenderPassOutput(const RenderPassOutput &) = delete;
    RenderPassOutput &operator=(const RenderPassOutput &) = delete;

    /**
     * @brief Initialize the output with given dimensions
     * @param name Pass name for identification
     * @param width Width in pixels
     * @param height Height in pixels
     * @param format Vulkan format (default RGBA8)
     * @param includeDepth Whether to include depth output
     * @return true if successful
     */
    bool Initialize(const std::string &name, uint32_t width, uint32_t height,
                    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM, bool includeDepth = true);

    /**
     * @brief Resize the output (recreates resources)
     */
    void Resize(uint32_t width, uint32_t height);

    /**
     * @brief Cleanup all resources
     */
    void Cleanup();

    // ========================================================================
    // Texture Access
    // ========================================================================

    /**
     * @brief Get color image view (for use as input in subsequent passes)
     */
    [[nodiscard]] VkImageView GetColorImageView() const
    {
        return m_colorImageView;
    }

    /**
     * @brief Get depth image view
     */
    [[nodiscard]] VkImageView GetDepthImageView() const
    {
        return m_depthImageView;
    }

    /**
     * @brief Get color image (for barriers)
     */
    [[nodiscard]] VkImage GetColorImage() const
    {
        return m_colorImage;
    }

    /**
     * @brief Get sampler for the color texture
     */
    [[nodiscard]] VkSampler GetSampler() const
    {
        return m_sampler;
    }

    /**
     * @brief Get ImGui texture ID for display
     */
    [[nodiscard]] uint64_t GetImGuiTextureId() const
    {
        return reinterpret_cast<uint64_t>(m_imguiDescriptorSet);
    }

    // ========================================================================
    // CPU Readback
    // ========================================================================

    /**
     * @brief Read color pixels back to CPU memory
     *
     * This is a blocking operation that waits for GPU completion.
     * For better performance, use RequestReadback + GetReadbackResult for async.
     *
     * @param outData Output vector to receive pixel data (RGBA8)
     * @return true if successful
     */
    bool ReadbackColorPixels(std::vector<uint8_t> &outData);

    /**
     * @brief Read depth buffer back to CPU memory
     * @param outData Output vector to receive depth data (float32)
     * @return true if successful
     */
    bool ReadbackDepthPixels(std::vector<float> &outData);

    /**
     * @brief Request async readback (non-blocking)
     *
     * Call this after rendering, then later call IsReadbackComplete and GetReadbackResult.
     */
    void RequestReadback();

    /**
     * @brief Check if async readback is complete
     */
    [[nodiscard]] bool IsReadbackComplete() const;

    /**
     * @brief Get async readback result (call after IsReadbackComplete returns true)
     * @param outData Output vector to receive pixel data
     * @return true if successful
     */
    bool GetReadbackResult(std::vector<uint8_t> &outData);

    // ========================================================================
    // Properties
    // ========================================================================

    [[nodiscard]] const std::string &GetName() const
    {
        return m_name;
    }
    [[nodiscard]] uint32_t GetWidth() const
    {
        return m_width;
    }
    [[nodiscard]] uint32_t GetHeight() const
    {
        return m_height;
    }
    [[nodiscard]] VkFormat GetFormat() const
    {
        return m_format;
    }
    [[nodiscard]] bool IsReady() const
    {
        return m_isInitialized;
    }
    [[nodiscard]] bool HasDepth() const
    {
        return m_depthImage != VK_NULL_HANDLE;
    }

  private:
    void CreateColorAttachment();
    void CreateDepthAttachment();
    void CreateStagingBuffer();
    void CreateImGuiDescriptor();
    void CleanupResources();

    InfVkCoreModular *m_vkCore = nullptr;

    std::string m_name;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    VkFormat m_format = VK_FORMAT_R8G8B8A8_UNORM;
    bool m_includeDepth = true;
    bool m_isInitialized = false;

    // Color attachment
    VkImage m_colorImage = VK_NULL_HANDLE;
    VmaAllocation m_colorAllocation = VK_NULL_HANDLE;
    VkImageView m_colorImageView = VK_NULL_HANDLE;

    // Depth attachment
    VkImage m_depthImage = VK_NULL_HANDLE;
    VmaAllocation m_depthAllocation = VK_NULL_HANDLE;
    VkImageView m_depthImageView = VK_NULL_HANDLE;

    // For texture sampling
    VkSampler m_sampler = VK_NULL_HANDLE;
    VkDescriptorSet m_imguiDescriptorSet = VK_NULL_HANDLE;

    // Staging buffers for CPU readback
    VkBuffer m_colorStagingBuffer = VK_NULL_HANDLE;
    VmaAllocation m_colorStagingAllocation = VK_NULL_HANDLE;
    VkDeviceSize m_colorStagingSize = 0;

    VkBuffer m_depthStagingBuffer = VK_NULL_HANDLE;
    VmaAllocation m_depthStagingAllocation = VK_NULL_HANDLE;
    VkDeviceSize m_depthStagingSize = 0;

    // Async readback
    VkFence m_readbackFence = VK_NULL_HANDLE;
    bool m_readbackPending = false;
};

} // namespace infengine
