/**
 * @file OutlineRenderer.cpp
 * @brief Post-process selection outline renderer implementation
 *
 * Extracted from InfVkCoreModular.cpp (Phase 1 refactoring).
 */

#include "OutlineRenderer.h"
#include "InfVkCoreModular.h"
#include "SceneRenderTarget.h"

#include <core/error/InfError.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstring>

namespace infengine
{

// ============================================================================
// Destructor
// ============================================================================

OutlineRenderer::~OutlineRenderer()
{
    Cleanup();
}

// ============================================================================
// Lifecycle
// ============================================================================

bool OutlineRenderer::Initialize(InfVkCoreModular *core, SceneRenderTarget *sceneTarget)
{
    if (m_resourcesReady)
        return true;

    if (!core || !sceneTarget || !sceneTarget->IsReady()) {
        INFLOG_WARN("OutlineRenderer::Initialize: core or SceneRenderTarget not ready");
        return false;
    }

    m_core = core;
    m_sceneRenderTarget = sceneTarget;

    // Check if outline shaders are loaded
    if (!m_core->HasShader("outline_mask", "vertex") || !m_core->HasShader("outline_mask", "fragment") ||
        !m_core->HasShader("outline_composite", "vertex") || !m_core->HasShader("outline_composite", "fragment")) {
        INFLOG_WARN("OutlineRenderer::Initialize: outline shaders not loaded yet");
        return false;
    }

    CreateOutlineMaskRenderPass();
    CreateOutlineCompositeRenderPass();
    CreateOutlineFramebuffers();
    CreateOutlineDescriptorResources();
    CreateOutlinePipelines();

    m_resourcesReady = true;
    INFLOG_INFO("OutlineRenderer: post-process outline resources initialized");
    return true;
}

void OutlineRenderer::Cleanup()
{
    VkDevice device = m_core ? m_core->GetDevice() : VK_NULL_HANDLE;
    if (device == VK_NULL_HANDLE)
        return;

    if (!m_core->IsShuttingDown()) {
        m_core->GetDeviceContext().WaitIdle();
    }

    if (m_outlineMaskPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_outlineMaskPipeline, nullptr);
        m_outlineMaskPipeline = VK_NULL_HANDLE;
    }
    if (m_outlineMaskPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_outlineMaskPipelineLayout, nullptr);
        m_outlineMaskPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_outlineCompositePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, m_outlineCompositePipeline, nullptr);
        m_outlineCompositePipeline = VK_NULL_HANDLE;
    }
    if (m_outlineCompositePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, m_outlineCompositePipelineLayout, nullptr);
        m_outlineCompositePipelineLayout = VK_NULL_HANDLE;
    }
    if (m_outlineMaskDescSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_outlineMaskDescSetLayout, nullptr);
        m_outlineMaskDescSetLayout = VK_NULL_HANDLE;
    }
    if (m_outlineCompositeDescSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, m_outlineCompositeDescSetLayout, nullptr);
        m_outlineCompositeDescSetLayout = VK_NULL_HANDLE;
    }
    if (m_outlineDescPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, m_outlineDescPool, nullptr);
        m_outlineDescPool = VK_NULL_HANDLE;
    }
    if (m_outlineMaskFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, m_outlineMaskFramebuffer, nullptr);
        m_outlineMaskFramebuffer = VK_NULL_HANDLE;
    }
    if (m_outlineCompositeFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, m_outlineCompositeFramebuffer, nullptr);
        m_outlineCompositeFramebuffer = VK_NULL_HANDLE;
    }
    if (m_outlineMaskRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_outlineMaskRenderPass, nullptr);
        m_outlineMaskRenderPass = VK_NULL_HANDLE;
    }
    if (m_outlineCompositeRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_outlineCompositeRenderPass, nullptr);
        m_outlineCompositeRenderPass = VK_NULL_HANDLE;
    }

    m_outlineMaskDescSet = VK_NULL_HANDLE;
    m_outlineCompositeDescSet = VK_NULL_HANDLE;
    m_resourcesReady = false;
}

void OutlineRenderer::OnResize(uint32_t width, uint32_t height)
{
    if (!m_resourcesReady)
        return;

    VkDevice device = m_core->GetDevice();
    m_core->GetDeviceContext().WaitIdle();

    // Destroy old framebuffers
    if (m_outlineMaskFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, m_outlineMaskFramebuffer, nullptr);
        m_outlineMaskFramebuffer = VK_NULL_HANDLE;
    }
    if (m_outlineCompositeFramebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, m_outlineCompositeFramebuffer, nullptr);
        m_outlineCompositeFramebuffer = VK_NULL_HANDLE;
    }

    CreateOutlineFramebuffers();

    // Update composite descriptor set with new mask image view
    VkDescriptorImageInfo imageInfo{};
    imageInfo.sampler = m_sceneRenderTarget->GetOutlineMaskSampler();
    imageInfo.imageView = m_sceneRenderTarget->GetOutlineMaskImageView();
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_outlineCompositeDescSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

// ============================================================================
// Rendering
// ============================================================================

void OutlineRenderer::RecordCommands(VkCommandBuffer cmdBuf, const std::vector<DrawCall> &drawCalls)
{
    if (!m_resourcesReady || m_outlineObjectId == 0 || !m_sceneRenderTarget)
        return;

    RenderOutlineMask(cmdBuf, drawCalls);
    RenderOutlineComposite(cmdBuf);
}

void OutlineRenderer::RecordNoOutlineBarrier(VkCommandBuffer cmdBuf)
{
    if (!m_sceneRenderTarget)
        return;

    // No outline active: transition the scene color image from
    // COLOR_ATTACHMENT_OPTIMAL to SHADER_READ_ONLY_OPTIMAL for ImGui sampling.
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = m_sceneRenderTarget->GetColorImage();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
}

// ============================================================================
// Internal: Vulkan Resource Creation
// ============================================================================

void OutlineRenderer::CreateOutlineMaskRenderPass()
{
    // Single color attachment: mask (R8G8B8A8_UNORM, clear to black, store for later sampling).
    // No depth attachment — the SceneRenderTarget depth is NOT shared with the scene RenderGraph
    // (it creates a transient depth internally), so we cannot do occlusion testing.
    // This matches Blender behavior: selection outline is always visible (X-ray).
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = nullptr; // No depth

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_core->GetDevice(), &rpInfo, nullptr, &m_outlineMaskRenderPass) != VK_SUCCESS) {
        INFLOG_ERROR("OutlineRenderer: Failed to create outline mask render pass");
    }
}

void OutlineRenderer::CreateOutlineCompositeRenderPass()
{
    // Single color attachment: scene color (load existing, alpha-blend outline on top)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD; // Preserve scene color
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    // Dependency: mask pass must finish before composite reads the mask texture
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_core->GetDevice(), &rpInfo, nullptr, &m_outlineCompositeRenderPass) != VK_SUCCESS) {
        INFLOG_ERROR("OutlineRenderer: Failed to create outline composite render pass");
    }
}

void OutlineRenderer::CreateOutlineFramebuffers()
{
    uint32_t w = m_sceneRenderTarget->GetWidth();
    uint32_t h = m_sceneRenderTarget->GetHeight();

    // Mask framebuffer: mask color only (no depth — always-visible outline)
    {
        VkImageView attachment = m_sceneRenderTarget->GetOutlineMaskImageView();

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_outlineMaskRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &attachment;
        fbInfo.width = w;
        fbInfo.height = h;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(m_core->GetDevice(), &fbInfo, nullptr, &m_outlineMaskFramebuffer) != VK_SUCCESS) {
            INFLOG_ERROR("OutlineRenderer: Failed to create outline mask framebuffer");
        }
    }

    // Composite framebuffer: scene color
    {
        VkImageView attachment = m_sceneRenderTarget->GetColorImageView();

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_outlineCompositeRenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &attachment;
        fbInfo.width = w;
        fbInfo.height = h;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(m_core->GetDevice(), &fbInfo, nullptr, &m_outlineCompositeFramebuffer) != VK_SUCCESS) {
            INFLOG_ERROR("OutlineRenderer: Failed to create outline composite framebuffer");
        }
    }
}

void OutlineRenderer::CreateOutlineDescriptorResources()
{
    VkDevice device = m_core->GetDevice();

    // --- Descriptor pool (2 sets: mask UBO + composite sampler) ---
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 2;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_outlineDescPool) != VK_SUCCESS) {
        INFLOG_ERROR("OutlineRenderer: Failed to create outline descriptor pool");
        return;
    }

    // --- Mask descriptor set layout: binding 0 = UBO (scene VP matrices) ---
    {
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboBinding;

        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_outlineMaskDescSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_outlineDescPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_outlineMaskDescSetLayout;

        vkAllocateDescriptorSets(device, &allocInfo, &m_outlineMaskDescSet);

        // Write scene UBO to binding 0
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_core->GetUniformBuffer(0);
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(UniformBufferObject);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_outlineMaskDescSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    // --- Composite descriptor set layout: binding 0 = sampler (mask texture) ---
    {
        VkDescriptorSetLayoutBinding samplerBinding{};
        samplerBinding.binding = 0;
        samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        samplerBinding.descriptorCount = 1;
        samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &samplerBinding;

        vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_outlineCompositeDescSetLayout);

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_outlineDescPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_outlineCompositeDescSetLayout;

        vkAllocateDescriptorSets(device, &allocInfo, &m_outlineCompositeDescSet);

        // Write mask texture to binding 0
        VkDescriptorImageInfo imageInfo{};
        imageInfo.sampler = m_sceneRenderTarget->GetOutlineMaskSampler();
        imageInfo.imageView = m_sceneRenderTarget->GetOutlineMaskImageView();
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_outlineCompositeDescSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }
}

void OutlineRenderer::CreateOutlinePipelines()
{
    VkDevice device = m_core->GetDevice();

    // ========================================================================
    // Mask Pipeline — renders selected object as white silhouette
    // ========================================================================
    {
        // Pipeline layout: 1 descriptor set (UBO at binding 0) + push constants (128 bytes, vertex)
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushRange.offset = 0;
        pushRange.size = 128; // 2 x mat4

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_outlineMaskDescSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_outlineMaskPipelineLayout);

        // Shader stages
        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = m_core->GetShaderModule("outline_mask", "vertex");
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = m_core->GetShaderModule("outline_mask", "fragment");
        fragStage.pName = "main";

        std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

        // Vertex input: same as scene mesh (Vertex struct)
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

        // Viewport + scissor (dynamic)
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        std::array<VkDynamicState, 2> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
        dynamicState.pDynamicStates = dynStates.data();

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // Rasterization: no culling — supports all mesh winding orders
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1.0f;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        // No depth test — scene depth not shared, outline is always visible (like Blender)
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttach{};
        colorBlendAttach.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttach.blendEnable = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &colorBlendAttach;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_outlineMaskPipelineLayout;
        pipelineInfo.renderPass = m_outlineMaskRenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_outlineMaskPipeline) !=
            VK_SUCCESS) {
            INFLOG_ERROR("OutlineRenderer: Failed to create outline mask pipeline");
        }
    }

    // ========================================================================
    // Composite Pipeline — fullscreen edge detection + alpha blend
    // ========================================================================
    {
        // Pipeline layout: 1 descriptor set (sampler at binding 0) + push constants (32 bytes, fragment)
        VkPushConstantRange pushRange{};
        pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pushRange.offset = 0;
        pushRange.size = 32; // vec4 color + vec2 texelSize + float width + float padding

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &m_outlineCompositeDescSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;

        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &m_outlineCompositePipelineLayout);

        // Shader stages
        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = m_core->GetShaderModule("outline_composite", "vertex");
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = m_core->GetShaderModule("outline_composite", "fragment");
        fragStage.pName = "main";

        std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

        // No vertex input (fullscreen triangle is procedural)
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        // Viewport + scissor (dynamic)
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        std::array<VkDynamicState, 2> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynStates.size());
        dynamicState.pDynamicStates = dynStates.data();

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        // Rasterization: no culling for fullscreen triangle
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1.0f;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        // No depth test
        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // Alpha blending for outline compositing
        VkPipelineColorBlendAttachmentState colorBlendAttach{};
        colorBlendAttach.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttach.blendEnable = VK_TRUE;
        colorBlendAttach.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttach.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttach.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttach.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttach.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttach.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &colorBlendAttach;

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages = stages.data();
        pipelineInfo.pVertexInputState = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &raster;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlend;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_outlineCompositePipelineLayout;
        pipelineInfo.renderPass = m_outlineCompositeRenderPass;
        pipelineInfo.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_outlineCompositePipeline) !=
            VK_SUCCESS) {
            INFLOG_ERROR("OutlineRenderer: Failed to create outline composite pipeline");
        }
    }
}

// ============================================================================
// Internal: Mask Pass
// ============================================================================

void OutlineRenderer::RenderOutlineMask(VkCommandBuffer cmdBuf, const std::vector<DrawCall> &drawCalls)
{
    uint32_t w = m_sceneRenderTarget->GetWidth();
    uint32_t h = m_sceneRenderTarget->GetHeight();

    // Begin mask render pass (clears mask to black, no depth)
    VkClearValue clearValue{};
    clearValue.color = {{0.0f, 0.0f, 0.0f, 0.0f}};

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = m_outlineMaskRenderPass;
    rpBegin.framebuffer = m_outlineMaskFramebuffer;
    rpBegin.renderArea.offset = {0, 0};
    rpBegin.renderArea.extent = {w, h};
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearValue;

    vkCmdBeginRenderPass(cmdBuf, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(w);
    viewport.height = static_cast<float>(h);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {w, h};
    vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

    // Bind mask pipeline
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_outlineMaskPipeline);

    // Bind mask descriptor set (scene UBO)
    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_outlineMaskPipelineLayout, 0, 1,
                            &m_outlineMaskDescSet, 0, nullptr);

    // Render the selected object
    for (const auto &dc : drawCalls) {
        if (dc.objectId != m_outlineObjectId)
            continue;

        // Get per-object buffer via core accessor
        VkBuffer vertBuf = m_core->GetObjectVertexBuffer(dc.objectId);
        VkBuffer idxBuf = m_core->GetObjectIndexBuffer(dc.objectId);

        if (vertBuf == VK_NULL_HANDLE || idxBuf == VK_NULL_HANDLE)
            continue;

        VkBuffer vertBuffers[] = {vertBuf};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmdBuf, 0, 1, vertBuffers, offsets);
        vkCmdBindIndexBuffer(cmdBuf, idxBuf, 0, VK_INDEX_TYPE_UINT32);

        // Push per-object model matrix + normal matrix
        struct PushConstants
        {
            glm::mat4 model;
            glm::mat4 normalMat;
        };

        PushConstants pushData;
        pushData.model = dc.worldMatrix;
        glm::mat3 normalMat3 = glm::transpose(glm::inverse(glm::mat3(dc.worldMatrix)));
        pushData.normalMat = glm::mat4(normalMat3);

        vkCmdPushConstants(cmdBuf, m_outlineMaskPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants),
                           &pushData);

        // Per-object VBO: indexStart is always 0 for per-object buffers
        bool usePerObject = (m_core->GetObjectVertexBuffer(dc.objectId) != VK_NULL_HANDLE);
        vkCmdDrawIndexed(cmdBuf, dc.indexCount, 1, usePerObject ? 0 : dc.indexStart, 0, 0);
    }

    vkCmdEndRenderPass(cmdBuf);
}

// ============================================================================
// Internal: Composite Pass
// ============================================================================

void OutlineRenderer::RenderOutlineComposite(VkCommandBuffer cmdBuf)
{
    uint32_t w = m_sceneRenderTarget->GetWidth();
    uint32_t h = m_sceneRenderTarget->GetHeight();

    // Barrier: transition scene color to COLOR_ATTACHMENT_OPTIMAL for compositing
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_sceneRenderTarget->GetColorImage();
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Begin composite render pass
    VkClearValue dummyClear{};
    dummyClear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = m_outlineCompositeRenderPass;
    rpBegin.framebuffer = m_outlineCompositeFramebuffer;
    rpBegin.renderArea.offset = {0, 0};
    rpBegin.renderArea.extent = {w, h};
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &dummyClear;

    vkCmdBeginRenderPass(cmdBuf, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    // Set viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(w);
    viewport.height = static_cast<float>(h);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmdBuf, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {w, h};
    vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

    // Bind composite pipeline
    vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_outlineCompositePipeline);

    // Bind composite descriptor set (mask texture sampler)
    vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, m_outlineCompositePipelineLayout, 0, 1,
                            &m_outlineCompositeDescSet, 0, nullptr);

    // Push constants: outline color, texel size, outline width
    struct CompositePushConstants
    {
        glm::vec4 outlineColor;
        glm::vec2 texelSize;
        float outlineWidth;
        float _padding;
    };

    CompositePushConstants pushData;
    pushData.outlineColor = m_outlineColor;
    pushData.texelSize = glm::vec2(1.0f / static_cast<float>(w), 1.0f / static_cast<float>(h));
    pushData.outlineWidth = m_outlinePixelWidth;
    pushData._padding = 0.0f;

    vkCmdPushConstants(cmdBuf, m_outlineCompositePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(CompositePushConstants), &pushData);

    // Draw fullscreen triangle (3 vertices, no vertex buffer)
    vkCmdDraw(cmdBuf, 3, 1, 0, 0);

    vkCmdEndRenderPass(cmdBuf);

    // Post-composite barrier: transition scene color to SHADER_READ_ONLY_OPTIMAL for ImGui
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = m_sceneRenderTarget->GetColorImage();
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
}

} // namespace infengine
