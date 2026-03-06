#include "SceneRenderTarget.h"
#include "InfVkCoreModular.h"
#include "vk/VkDeviceContext.h"
#include <array>
#include <backends/imgui_impl_vulkan.h>
#include <core/log/InfLog.h>

namespace infengine
{

SceneRenderTarget::SceneRenderTarget(InfVkCoreModular *vkCore) : m_vkCore(vkCore)
{
}

SceneRenderTarget::~SceneRenderTarget()
{
    Cleanup();
}

bool SceneRenderTarget::Initialize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0) {
        INFLOG_ERROR("SceneRenderTarget: Invalid dimensions ", width, "x", height);
        return false;
    }

    m_width = width;
    m_height = height;

    try {
        CreateColorAttachment();
        if (m_msaaSampleCount != VK_SAMPLE_COUNT_1_BIT) {
            CreateMsaaColorAttachment();
        }
        CreateDepthAttachment();
        CreateOutlineMaskAttachment();
        // NOTE: Framebuffer is no longer created here - RenderGraph creates its own
        CreateImGuiDescriptor();
        m_isInitialized = true;
        // INFLOG_INFO("SceneRenderTarget initialized: ", width, "x", height);
        return true;
    } catch (const std::exception &e) {
        INFLOG_ERROR("SceneRenderTarget initialization failed: ", e.what());
        CleanupResources();
        return false;
    }
}

void SceneRenderTarget::Resize(uint32_t width, uint32_t height)
{
    if (width == m_width && height == m_height) {
        return;
    }

    // Wait for device to be idle before recreating resources
    m_vkCore->GetDeviceContext().WaitIdle();

    CleanupResources();
    Initialize(width, height);
}

VkFormat SceneRenderTarget::GetDepthFormat() const
{
    if (m_vkCore) {
        return m_vkCore->GetDeviceContext().FindDepthFormat();
    }
    return VK_FORMAT_D32_SFLOAT;
}

void SceneRenderTarget::CreateColorAttachment()
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_width;
    imageInfo.extent.height = m_height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // TRANSFER_DST_BIT needed for explicit MSAA resolve via vkCmdResolveImage
    imageInfo.usage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    // Create image + allocate memory via VMA (combined create+alloc+bind)
    VmaAllocator allocator = m_vkCore->GetDeviceContext().GetVmaAllocator();
    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkResult result =
        vmaCreateImage(allocator, &imageInfo, &allocCreateInfo, &m_colorImage, &m_colorAllocation, nullptr);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create color image via VMA");
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_colorImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_vkCore->GetDevice(), &viewInfo, nullptr, &m_colorImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create color image view");
    }

    // Transition to shader read optimal initially
    VkCommandBuffer cmdBuf = m_vkCore->BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_colorImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);

    m_vkCore->EndSingleTimeCommands(cmdBuf);
}

void SceneRenderTarget::CreateMsaaColorAttachment()
{
    const VkSampleCountFlagBits msaaSamples = m_msaaSampleCount;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_width;
    imageInfo.extent.height = m_height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // TRANSFER_SRC_BIT needed for explicit MSAA resolve via vkCmdResolveImage
    // (cannot use TRANSIENT_ATTACHMENT_BIT with TRANSFER_SRC_BIT)
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = msaaSamples;

    VmaAllocator allocator = m_vkCore->GetDeviceContext().GetVmaAllocator();
    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkResult result =
        vmaCreateImage(allocator, &imageInfo, &allocCreateInfo, &m_msaaColorImage, &m_msaaColorAllocation, nullptr);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create MSAA color image via VMA");
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_msaaColorImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_vkCore->GetDevice(), &viewInfo, nullptr, &m_msaaColorImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create MSAA color image view");
    }
}

void SceneRenderTarget::CreateDepthAttachment()
{
    VkFormat depthFormat = m_vkCore->GetDeviceContext().FindDepthFormat();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = m_width;
    imageInfo.extent.height = m_height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = m_msaaSampleCount; // Match MSAA color sample count

    VmaAllocator allocator = m_vkCore->GetDeviceContext().GetVmaAllocator();
    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    VkResult result =
        vmaCreateImage(allocator, &imageInfo, &allocCreateInfo, &m_depthImage, &m_depthAllocation, nullptr);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image via VMA");
    }

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_depthImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_vkCore->GetDevice(), &viewInfo, nullptr, &m_depthImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image view");
    }

    // Transition depth image to depth-stencil attachment optimal
    VkCommandBuffer cmdBuf = m_vkCore->BeginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_depthImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (vk::VkDeviceContext::HasStencilComponent(depthFormat)) {
        barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);

    m_vkCore->EndSingleTimeCommands(cmdBuf);
}

void SceneRenderTarget::CreateImGuiDescriptor()
{
    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(m_vkCore->GetDevice(), &samplerInfo, nullptr, &m_sampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create scene texture sampler");
    }

    // Create ImGui descriptor set for the texture
    m_imguiDescriptorSet =
        ImGui_ImplVulkan_AddTexture(m_sampler, m_colorImageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (m_imguiDescriptorSet == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to create ImGui descriptor set for scene texture");
    }
}

void SceneRenderTarget::CreateOutlineMaskAttachment()
{
    VkDevice device = m_vkCore->GetDevice();

    // Single-channel mask texture (R8_UNORM) — stores 1.0 where selected object is visible
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {m_width, m_height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM; // Use RGBA for render pass compatibility
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    VmaAllocator allocator = m_vkCore->GetDeviceContext().GetVmaAllocator();
    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    vmaCreateImage(allocator, &imageInfo, &allocCreateInfo, &m_outlineMaskImage, &m_outlineMaskAllocation, nullptr);

    // Image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_outlineMaskImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    vkCreateImageView(device, &viewInfo, nullptr, &m_outlineMaskImageView);

    // Sampler for composite pass
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    vkCreateSampler(device, &samplerInfo, nullptr, &m_outlineMaskSampler);

    // Transition to shader-read initially (will be transitioned at runtime)
    VkCommandBuffer cmdBuf = m_vkCore->BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_outlineMaskImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
    m_vkCore->EndSingleTimeCommands(cmdBuf);
}

void SceneRenderTarget::CleanupResources()
{
    VkDevice device = m_vkCore->GetDevice();
    VmaAllocator allocator = m_vkCore->GetDeviceContext().GetVmaAllocator();

    if (m_imguiDescriptorSet != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(m_imguiDescriptorSet);
        m_imguiDescriptorSet = VK_NULL_HANDLE;
    }

    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_sampler, nullptr);
        m_sampler = VK_NULL_HANDLE;
    }

    // Cleanup outline mask resources
    if (m_outlineMaskSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_outlineMaskSampler, nullptr);
        m_outlineMaskSampler = VK_NULL_HANDLE;
    }
    if (m_outlineMaskImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_outlineMaskImageView, nullptr);
        m_outlineMaskImageView = VK_NULL_HANDLE;
    }
    if (m_outlineMaskImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, m_outlineMaskImage, m_outlineMaskAllocation);
        m_outlineMaskImage = VK_NULL_HANDLE;
        m_outlineMaskAllocation = VK_NULL_HANDLE;
    }

    // Cleanup MSAA color resources
    if (m_msaaColorImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_msaaColorImageView, nullptr);
        m_msaaColorImageView = VK_NULL_HANDLE;
    }
    if (m_msaaColorImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, m_msaaColorImage, m_msaaColorAllocation);
        m_msaaColorImage = VK_NULL_HANDLE;
        m_msaaColorAllocation = VK_NULL_HANDLE;
    }

    if (m_depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }

    if (m_depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, m_depthImage, m_depthAllocation);
        m_depthImage = VK_NULL_HANDLE;
        m_depthAllocation = VK_NULL_HANDLE;
    }

    if (m_colorImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_colorImageView, nullptr);
        m_colorImageView = VK_NULL_HANDLE;
    }

    if (m_colorImage != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator, m_colorImage, m_colorAllocation);
        m_colorImage = VK_NULL_HANDLE;
        m_colorAllocation = VK_NULL_HANDLE;
    }

    m_isInitialized = false;
}

void SceneRenderTarget::Cleanup()
{
    if (m_vkCore && m_vkCore->GetDevice() != VK_NULL_HANDLE) {
        if (!m_vkCore->IsShuttingDown()) {
            m_vkCore->GetDeviceContext().WaitIdle();
        }
        CleanupResources();
    }
}

} // namespace infengine
