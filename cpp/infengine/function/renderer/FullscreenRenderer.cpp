/**
 * @file FullscreenRenderer.cpp
 * @brief Implementation of the FullscreenRenderer utility
 */

#include "FullscreenRenderer.h"
#include "InfVkCoreModular.h"
#include "vk/VkDeviceContext.h"
#include "vk/VkPipelineManager.h"
#include <core/error/InfError.h>

namespace infengine
{

// ============================================================================
// Lifecycle
// ============================================================================

FullscreenRenderer::~FullscreenRenderer()
{
    Destroy();
}

void FullscreenRenderer::Initialize(InfVkCoreModular *vkCore)
{
    if (!vkCore) {
        INFLOG_ERROR("FullscreenRenderer::Initialize: null vkCore");
        return;
    }
    m_vkCore = vkCore;
    m_device = vkCore->GetDevice();

    CreateLinearSampler();
    CreateDescriptorPool();

    INFLOG_INFO("FullscreenRenderer initialized");
}

void FullscreenRenderer::Destroy()
{
    if (m_device == VK_NULL_HANDLE)
        return;

    vkDeviceWaitIdle(m_device);

    for (auto &[key, entry] : m_pipelineCache) {
        if (entry.pipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(m_device, entry.pipeline, nullptr);
        if (entry.layoutOwned && entry.layout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(m_device, entry.layout, nullptr);
        if (entry.descSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(m_device, entry.descSetLayout, nullptr);
    }
    m_pipelineCache.clear();

    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }

    if (m_linearSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_linearSampler, nullptr);
        m_linearSampler = VK_NULL_HANDLE;
    }

    m_device = VK_NULL_HANDLE;
    m_vkCore = nullptr;
}

// ============================================================================
// Pipeline management
// ============================================================================

const FullscreenPipelineEntry &FullscreenRenderer::EnsurePipeline(const FullscreenPipelineKey &key)
{
    auto it = m_pipelineCache.find(key);
    if (it != m_pipelineCache.end()) {
        return it->second;
    }

    auto entry = CreatePipeline(key);
    auto [insertIt, inserted] = m_pipelineCache.emplace(key, entry);
    return insertIt->second;
}

FullscreenPipelineEntry FullscreenRenderer::CreatePipeline(const FullscreenPipelineKey &key)
{
    FullscreenPipelineEntry entry{};

    if (!m_vkCore) {
        INFLOG_ERROR("FullscreenRenderer::CreatePipeline: not initialized");
        return entry;
    }

    auto &pipelineMgr = m_vkCore->GetPipelineManager();

    // ------------------------------------------------------------------
    // 1. Descriptor set layout: N combined image samplers (fragment)
    // ------------------------------------------------------------------
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (uint32_t i = 0; i < key.inputTextureCount; ++i) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = i;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        b.pImmutableSamplers = nullptr;
        bindings.push_back(b);
    }

    VkDescriptorSetLayoutCreateInfo layoutCI{};
    layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutCI.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutCI.pBindings = bindings.data();

    VkDescriptorSetLayout descSetLayout = VK_NULL_HANDLE;
    if (vkCreateDescriptorSetLayout(m_device, &layoutCI, nullptr, &descSetLayout) != VK_SUCCESS) {
        INFLOG_ERROR("FullscreenRenderer: Failed to create descriptor set layout for '", key.shaderName, "'");
        return entry;
    }
    entry.descSetLayout = descSetLayout;

    // ------------------------------------------------------------------
    // 2. Pipeline layout: 1 desc set + push constants (128 bytes, fragment)
    // ------------------------------------------------------------------
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(FullscreenPushConstants);

    VkPipelineLayoutCreateInfo pipeLayoutCI{};
    pipeLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeLayoutCI.setLayoutCount = 1;
    pipeLayoutCI.pSetLayouts = &descSetLayout;
    pipeLayoutCI.pushConstantRangeCount = 1;
    pipeLayoutCI.pPushConstantRanges = &pushRange;

    VkPipelineLayout pipeLayout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(m_device, &pipeLayoutCI, nullptr, &pipeLayout) != VK_SUCCESS) {
        INFLOG_ERROR("FullscreenRenderer: Failed to create pipeline layout for '", key.shaderName, "'");
        vkDestroyDescriptorSetLayout(m_device, descSetLayout, nullptr);
        entry.descSetLayout = VK_NULL_HANDLE;
        return entry;
    }
    entry.layout = pipeLayout;
    entry.layoutOwned = true;

    // ------------------------------------------------------------------
    // 3. Shader modules
    // ------------------------------------------------------------------
    // Try effect-specific vertex shader first; fall back to the shared
    // fullscreen_triangle vertex shader if not found.
    VkShaderModule vertModule = m_vkCore->GetShaderModule(key.shaderName, "vertex");
    if (vertModule == VK_NULL_HANDLE) {
        vertModule = m_vkCore->GetShaderModule("fullscreen_triangle", "vertex");
    }
    VkShaderModule fragModule = m_vkCore->GetShaderModule(key.shaderName, "fragment");

    if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
        INFLOG_ERROR("FullscreenRenderer: Missing shader modules for '", key.shaderName, "' (vert=",
                     (vertModule != VK_NULL_HANDLE ? "OK" : "MISSING"), ", frag=",
                     (fragModule != VK_NULL_HANDLE ? "OK" : "MISSING"), ")");
        vkDestroyPipelineLayout(m_device, pipeLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_device, descSetLayout, nullptr);
        entry.layout = VK_NULL_HANDLE;
        entry.layoutOwned = false;
        entry.descSetLayout = VK_NULL_HANDLE;
        return entry;
    }

    // ------------------------------------------------------------------
    // 4. Graphics pipeline (no vertex input, no depth, no cull)
    // ------------------------------------------------------------------
    // Shader stages
    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    // Empty vertex input (procedural fullscreen triangle)
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Dynamic viewport + scissor
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    // Rasterization: no cull, fill
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    // Multisample
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = key.samples;

    // No depth
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    // Color blend: no blend (overwrite), single attachment
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    // Create pipeline
    VkGraphicsPipelineCreateInfo pipelineCI{};
    pipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCI.stageCount = 2;
    pipelineCI.pStages = shaderStages;
    pipelineCI.pVertexInputState = &vertexInput;
    pipelineCI.pInputAssemblyState = &inputAssembly;
    pipelineCI.pViewportState = &viewportState;
    pipelineCI.pRasterizationState = &rasterizer;
    pipelineCI.pMultisampleState = &multisampling;
    pipelineCI.pDepthStencilState = &depthStencil;
    pipelineCI.pColorBlendState = &colorBlending;
    pipelineCI.pDynamicState = &dynamicState;
    pipelineCI.layout = pipeLayout;
    pipelineCI.renderPass = key.renderPass;
    pipelineCI.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineCI, nullptr, &pipeline) != VK_SUCCESS) {
        INFLOG_ERROR("FullscreenRenderer: Failed to create pipeline for '", key.shaderName, "'");
        vkDestroyPipelineLayout(m_device, pipeLayout, nullptr);
        vkDestroyDescriptorSetLayout(m_device, descSetLayout, nullptr);
        entry.layout = VK_NULL_HANDLE;
        entry.layoutOwned = false;
        entry.descSetLayout = VK_NULL_HANDLE;
        return entry;
    }
    entry.pipeline = pipeline;

    INFLOG_INFO("FullscreenRenderer: Created pipeline for '", key.shaderName, "' with ", key.inputTextureCount,
                " input(s)");
    return entry;
}

// ============================================================================
// Per-frame pool reset
// ============================================================================

void FullscreenRenderer::ResetPool()
{
    if (m_descriptorPool != VK_NULL_HANDLE) {
        vkResetDescriptorPool(m_device, m_descriptorPool, 0);
    }
}

// ============================================================================
// Descriptor set allocation
// ============================================================================

VkDescriptorSet FullscreenRenderer::AllocateDescriptorSet(VkDescriptorSetLayout layout,
                                                          const std::vector<VkImageView> &inputViews,
                                                          VkSampler sampler)
{
    if (m_descriptorPool == VK_NULL_HANDLE || layout == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descSet = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device, &allocInfo, &descSet) != VK_SUCCESS) {
        INFLOG_ERROR("FullscreenRenderer: Failed to allocate descriptor set");
        return VK_NULL_HANDLE;
    }

    // Write image descriptors
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorImageInfo> imageInfos(inputViews.size());

    for (uint32_t i = 0; i < inputViews.size(); ++i) {
        imageInfos[i].sampler = sampler;
        imageInfos[i].imageView = inputViews[i];
        imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descSet;
        write.dstBinding = i;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = 1;
        write.pImageInfo = &imageInfos[i];
        writes.push_back(write);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    return descSet;
}

// ============================================================================
// Draw
// ============================================================================

void FullscreenRenderer::Draw(VkCommandBuffer cmdBuf, const FullscreenPipelineEntry &entry, VkDescriptorSet descSet,
                              const FullscreenPushConstants &pushConstants, uint32_t pushConstantSize, uint32_t width,
                              uint32_t height)
{
    if (entry.pipeline == VK_NULL_HANDLE)
        return;

    // Set dynamic viewport and scissor
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

    // Bind pipeline
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, entry.pipeline);

    // Bind descriptor set (input textures)
    if (descSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, entry.layout, 0, 1, &descSet, 0, nullptr);
    }

    // Push constants
    if (pushConstantSize > 0) {
        vkCmdPushConstants(cmdBuf, entry.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushConstantSize,
                           pushConstants.values);
    }

    // Draw fullscreen triangle (3 vertices, no vertex buffer)
    vkCmdDraw(cmdBuf, 3, 1, 0, 0);
}

// ============================================================================
// Private helpers
// ============================================================================

void FullscreenRenderer::CreateDescriptorPool()
{
    // Pool sized for fullscreen effects — enough for many passes across a frame
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 256; // Up to 256 sampler bindings per frame

    VkDescriptorPoolCreateInfo poolCI{};
    poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolCI.maxSets = 128; // Up to 128 descriptor sets
    poolCI.poolSizeCount = 1;
    poolCI.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(m_device, &poolCI, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        INFLOG_ERROR("FullscreenRenderer: Failed to create descriptor pool");
    }
}

void FullscreenRenderer::CreateLinearSampler()
{
    VkSamplerCreateInfo samplerCI{};
    samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCI.magFilter = VK_FILTER_LINEAR;
    samplerCI.minFilter = VK_FILTER_LINEAR;
    samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerCI.mipLodBias = 0.0f;
    samplerCI.maxAnisotropy = 1.0f;
    samplerCI.minLod = 0.0f;
    samplerCI.maxLod = 0.0f;

    if (vkCreateSampler(m_device, &samplerCI, nullptr, &m_linearSampler) != VK_SUCCESS) {
        INFLOG_ERROR("FullscreenRenderer: Failed to create linear sampler");
    }
}

} // namespace infengine
