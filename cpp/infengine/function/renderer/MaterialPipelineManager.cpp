#include "MaterialPipelineManager.h"
#include "InfRenderStruct.h"
#include <core/log/InfLog.h>

namespace infengine
{

MaterialPipelineManager::~MaterialPipelineManager()
{
    Shutdown();
}

void MaterialPipelineManager::Initialize(VmaAllocator allocator, VkDevice device, VkPhysicalDevice physicalDevice,
                                         VkFormat colorFormat, VkFormat depthFormat, VkSampleCountFlagBits sampleCount,
                                         ShaderProgramCache &shaderProgramCache)
{
    m_device = device;
    m_physicalDevice = physicalDevice;
    m_colorFormat = colorFormat;
    m_depthFormat = depthFormat;
    m_sampleCount = sampleCount;
    m_shaderProgramCache = &shaderProgramCache;

    // Create internal compatible render pass for pipeline creation
    CreateInternalRenderPass();

    // Initialize shader program cache
    m_shaderProgramCache->Initialize(device);

    // Initialize material descriptor manager
    m_descriptorManager.Initialize(allocator, device, physicalDevice);

    // Create Vulkan pipeline cache for faster recreation
    VkPipelineCacheCreateInfo cacheCreateInfo{};
    cacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    if (vkCreatePipelineCache(m_device, &cacheCreateInfo, nullptr, &m_vkPipelineCache) != VK_SUCCESS) {
        INFLOG_WARN("Failed to create VkPipelineCache, pipeline recreation may be slower");
        m_vkPipelineCache = VK_NULL_HANDLE;
    }

    INFLOG_INFO("MaterialPipelineManager initialized with shader reflection support");
}

void MaterialPipelineManager::Shutdown(bool skipWaitIdle)
{
    if (m_device == VK_NULL_HANDLE) {
        return;
    }

    if (!skipWaitIdle) {
        vkDeviceWaitIdle(m_device);
    }

    // Shutdown descriptor manager
    m_descriptorManager.Shutdown();

    // Shutdown shader program cache
    if (m_shaderProgramCache) {
        m_shaderProgramCache->Shutdown();
        m_shaderProgramCache = nullptr;
    }

    // Destroy all pipelines
    for (auto &[hash, pipeline] : m_pipelineCache) {
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_device, pipeline, nullptr);
        }
    }
    m_pipelineCache.clear();

    // Clear stale Vulkan handles on material objects before dropping render data.
    // Otherwise materials may keep dangling pipeline/layout/descriptor handles
    // and attempt to use them on subsequent frames after MSAA reinitialization.
    for (auto &[name, data] : m_renderDataMap) {
        (void)name;
        if (!data || !data->material) {
            continue;
        }
        data->material->SetPipeline(VK_NULL_HANDLE);
        data->material->SetPipelineLayout(VK_NULL_HANDLE);
        data->material->SetDescriptorSet(VK_NULL_HANDLE);
        data->material->SetShaderProgram(nullptr);
    }

    // Clear render data. Shader modules stored in MaterialRenderData are
    // shallow copies of the handles owned by ShaderProgram, which were
    // already destroyed by ShaderProgramCache::Shutdown() above — do NOT
    // destroy them again here (double-free).
    m_renderDataMap.clear();

    // Destroy Vulkan pipeline cache
    if (m_vkPipelineCache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(m_device, m_vkPipelineCache, nullptr);
        m_vkPipelineCache = VK_NULL_HANDLE;
    }

    // Destroy internal render pass
    if (m_internalRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_device, m_internalRenderPass, nullptr);
        m_internalRenderPass = VK_NULL_HANDLE;
    }

    m_defaultRenderData = nullptr;
    m_device = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;

    INFLOG_INFO("MaterialPipelineManager shutdown");
}

VkShaderModule MaterialPipelineManager::CreateShaderModule(const std::vector<char> &code)
{
    if (code.empty()) {
        INFLOG_ERROR("Cannot create shader module from empty code");
        return VK_NULL_HANDLE;
    }

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        INFLOG_ERROR("Failed to create shader module");
        return VK_NULL_HANDLE;
    }

    return shaderModule;
}

MaterialRenderData *MaterialPipelineManager::GetRenderData(const std::string &materialName)
{
    auto it = m_renderDataMap.find(materialName);
    if (it != m_renderDataMap.end() && it->second->isValid) {
        return it->second.get();
    }
    return nullptr;
}

bool MaterialPipelineManager::HasRenderData(const std::string &materialName) const
{
    auto it = m_renderDataMap.find(materialName);
    return it != m_renderDataMap.end() && it->second->isValid;
}

MaterialRenderData *MaterialPipelineManager::GetDefaultRenderData()
{
    return m_defaultRenderData;
}

VkPipeline MaterialPipelineManager::GetCachedPipeline(size_t pipelineHash) const
{
    auto it = m_pipelineCache.find(pipelineHash);
    if (it != m_pipelineCache.end()) {
        return it->second;
    }
    return VK_NULL_HANDLE;
}

// ============================================================================
// New Shader Reflection API
// ============================================================================

MaterialRenderData *MaterialPipelineManager::GetOrCreateRenderDataWithReflection(
    std::shared_ptr<InfMaterial> material, const std::vector<char> &vertShaderCode,
    const std::vector<char> &fragShaderCode, const std::string &shaderId, VkBuffer sceneUBO, VkDeviceSize sceneUBOSize,
    VkBuffer lightingUBO, VkDeviceSize lightingUBOSize)
{
    if (!material) {
        INFLOG_ERROR("Cannot create render data for null material");
        return nullptr;
    }

    const std::string &name = material->GetName();

    // Check if already exists and valid
    auto it = m_renderDataMap.find(name);
    if (it != m_renderDataMap.end()) {
        size_t currentHash = material->GetPipelineHash();

        if (it->second->isValid) {
            if (it->second->pipelineHash == currentHash) {
                // Sync Vulkan handles to the (possibly new) material object.
                // This is critical when MaterialManager replaces the default material
                // with a freshly-deserialized one that has null handles.
                material->SetPipeline(it->second->pipeline);
                material->SetPipelineLayout(it->second->pipelineLayout);
                material->SetDescriptorSet(it->second->descriptorSet);
                material->SetShaderProgram(it->second->shaderProgram);
                material->ClearPipelineDirty();
                it->second->material = material; // update cached reference
                return it->second.get();
            }
            INFLOG_INFO("Material '", name, "' config changed, recreating pipeline");

            // Immediately clear stale Vulkan handles from the material.
            // If recreation fails below, the draw code will see pipeline == VK_NULL_HANDLE
            // and correctly fall back to the error material instead of rendering with
            // the old (now-incorrect) pipeline/descriptor set.
            material->SetPipeline(VK_NULL_HANDLE);
            material->SetPipelineLayout(VK_NULL_HANDLE);
            material->SetDescriptorSet(VK_NULL_HANDLE);
            material->SetShaderProgram(nullptr);
            it->second->isValid = false;
            // Update stored hash so we know when the user changes config again
            it->second->pipelineHash = currentHash;
        } else {
            // Render data exists but is invalid (previous creation attempt failed).
            // Only retry if the material config actually changed (user might have fixed it).
            if (it->second->pipelineHash == currentHash) {
                // Config unchanged since last failure — don't spam retries every frame.
                return nullptr;
            }
            INFLOG_INFO("Material '", name, "' config changed after failure, retrying pipeline creation");
            it->second->pipelineHash = currentHash;
        }
    }

    // Get or create shader program (with reflection)
    ShaderProgram *program = m_shaderProgramCache->GetOrCreateProgram(shaderId, vertShaderCode, fragShaderCode);
    if (!program || !program->IsValid()) {
        INFLOG_ERROR("Failed to get shader program for material: ", name);
        // Store an invalid render data entry so subsequent frames can detect
        // "already failed" and skip silently (no per-frame spam).
        if (m_renderDataMap.find(name) == m_renderDataMap.end()) {
            auto failedData = std::make_unique<MaterialRenderData>();
            failedData->material = material;
            failedData->pipelineHash = material->GetPipelineHash();
            failedData->isValid = false;
            m_renderDataMap[name] = std::move(failedData);
        }
        return nullptr;
    }

    // Create new render data
    auto renderData = std::make_unique<MaterialRenderData>();
    renderData->material = material;
    renderData->pipelineHash = material->GetPipelineHash();
    renderData->shaderProgram = program;
    renderData->vertModule = program->GetVertexModule();
    renderData->fragModule = program->GetFragmentModule();
    renderData->pipelineLayout = program->GetPipelineLayout();

    // Create or get material descriptor set
    renderData->materialDescSet = m_descriptorManager.GetOrCreateDescriptorSet(
        *material, *program, sceneUBO, sceneUBOSize, lightingUBO, lightingUBOSize);

    if (renderData->materialDescSet) {
        renderData->descriptorSet = renderData->materialDescSet->descriptorSet;
    }

    // Check pipeline cache first (use shader-based key)
    size_t pipelineKey = renderData->pipelineHash;
    VkPipeline cachedPipeline = GetCachedPipeline(pipelineKey);

    if (cachedPipeline != VK_NULL_HANDLE) {
        renderData->pipeline = cachedPipeline;
        renderData->isValid = true;
        INFLOG_DEBUG("Using cached pipeline for material: ", name);
    } else {
        // Create new pipeline using shader program
        renderData->pipeline = CreatePipelineWithProgram(program, material->GetRenderState());

        if (renderData->pipeline == VK_NULL_HANDLE) {
            INFLOG_ERROR("Failed to create pipeline for material: ", name);
            return nullptr;
        }

        renderData->isValid = true;

        // Cache the pipeline
        m_pipelineCache[pipelineKey] = renderData->pipeline;
        INFLOG_INFO("Created new reflected pipeline for material: ", name);
    }

    // Update material with pipeline info
    material->SetPipeline(renderData->pipeline);
    material->SetPipelineLayout(renderData->pipelineLayout);
    material->SetDescriptorSet(renderData->descriptorSet);
    material->SetShaderProgram(program); // Set shader program for reflection-based UBO updates
    material->ClearPipelineDirty();

    MaterialRenderData *result = renderData.get();
    m_renderDataMap[name] = std::move(renderData);

    return result;
}

VkPipeline MaterialPipelineManager::CreatePipelineWithProgram(ShaderProgram *program, const RenderState &renderState)
{
    if (!program || !program->IsValid()) {
        INFLOG_ERROR("Invalid shader program for pipeline creation");
        return VK_NULL_HANDLE;
    }

    // Debug log the cull mode being used
    const char *cullModeStr = "UNKNOWN";
    if (renderState.cullMode == VK_CULL_MODE_NONE)
        cullModeStr = "NONE";
    else if (renderState.cullMode == VK_CULL_MODE_FRONT_BIT)
        cullModeStr = "FRONT";
    else if (renderState.cullMode == VK_CULL_MODE_BACK_BIT)
        cullModeStr = "BACK";
    else if (renderState.cullMode == VK_CULL_MODE_FRONT_AND_BACK)
        cullModeStr = "FRONT_AND_BACK";
    INFLOG_DEBUG("CreatePipelineWithProgram: shader=", program->GetShaderId(), ", cullMode=", cullModeStr,
                 ", blendEnable=", renderState.blendEnable ? "true" : "false",
                 ", depthWrite=", renderState.depthWriteEnable ? "true" : "false",
                 ", depthTest=", renderState.depthTestEnable ? "true" : "false",
                 ", renderQueue=", renderState.renderQueue);

    // Shader stages
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = program->GetVertexModule();
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = program->GetFragmentModule();
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Dynamic state
    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Vertex input - using standard Vertex structure
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = renderState.topology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport state
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = renderState.polygonMode;
    rasterizer.lineWidth = renderState.lineWidth;
    rasterizer.cullMode = renderState.cullMode;
    rasterizer.frontFace = renderState.frontFace;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = m_sampleCount;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = renderState.blendEnable ? VK_TRUE : VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = renderState.srcColorBlendFactor;
    colorBlendAttachment.dstColorBlendFactor = renderState.dstColorBlendFactor;
    colorBlendAttachment.colorBlendOp = renderState.colorBlendOp;
    colorBlendAttachment.srcAlphaBlendFactor = renderState.srcAlphaBlendFactor;
    colorBlendAttachment.dstAlphaBlendFactor = renderState.dstAlphaBlendFactor;
    colorBlendAttachment.alphaBlendOp = renderState.alphaBlendOp;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Depth stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = renderState.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = renderState.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = renderState.depthCompareOp;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = renderState.stencilTestEnable ? VK_TRUE : VK_FALSE;

    // Create pipeline with shader program's layout
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = program->GetPipelineLayout(); // Use program's layout!
    pipelineInfo.renderPass = m_internalRenderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    INFLOG_DEBUG("Creating pipeline with renderPass=", reinterpret_cast<uint64_t>(m_internalRenderPass));

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(m_device, m_vkPipelineCache, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        INFLOG_ERROR("Failed to create graphics pipeline with shader program");
        return VK_NULL_HANDLE;
    }

    return pipeline;
}

void MaterialPipelineManager::UpdateMaterialProperties(const std::string &materialName, const InfMaterial &material)
{
    m_descriptorManager.UpdateMaterialUBO(materialName, material);
}

void MaterialPipelineManager::BindMaterialTexture(const std::string &materialName, uint32_t binding,
                                                  VkImageView imageView, VkSampler sampler)
{
    m_descriptorManager.BindTexture(materialName, binding, imageView, sampler);
}

void MaterialPipelineManager::SetDefaultTexture(VkImageView imageView, VkSampler sampler)
{
    m_descriptorManager.SetDefaultTexture(imageView, sampler);
}

void MaterialPipelineManager::SetDefaultNormalTexture(VkImageView imageView, VkSampler sampler)
{
    m_descriptorManager.SetDefaultNormalTexture(imageView, sampler);
}

void MaterialPipelineManager::CreateInternalRenderPass()
{
    // Create a minimal compatible render pass for pipeline creation
    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> colorRefs;
    VkAttachmentReference depthRef{};
    bool hasDepth = (m_depthFormat != VK_FORMAT_UNDEFINED);

    // Color attachment (MSAA)
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_colorFormat;
    colorAttachment.samples = m_sampleCount;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments.push_back(colorAttachment);

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorRefs.push_back(colorRef);

    // Depth attachment (optional)
    if (hasDepth) {
        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = m_depthFormat;
        depthAttachment.samples = m_sampleCount;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments.push_back(depthAttachment);

        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }

    // No resolve attachment — MSAA resolve is done explicitly via vkCmdResolveImage
    // after all draw calls complete, so that all objects benefit from MSAA.

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
    subpass.pColorAttachments = colorRefs.data();
    subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;
    subpass.pResolveAttachments = nullptr;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpInfo.pAttachments = attachments.data();
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(m_device, &rpInfo, nullptr, &m_internalRenderPass) != VK_SUCCESS) {
        INFLOG_ERROR("MaterialPipelineManager: Failed to create internal render pass");
        m_internalRenderPass = VK_NULL_HANDLE;
    }
}

void MaterialPipelineManager::InvalidateMaterialsUsingShader(const std::string &shaderId)
{
    INFLOG_INFO("Invalidating materials using shader: ", shaderId);

    // Helper to extract shader name from path
    auto extractShaderName = [](const std::string &path) -> std::string {
        if (path.empty())
            return "";
        size_t lastSlash = path.find_last_of("/\\");
        std::string fileName = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
        size_t dotPos = fileName.find_last_of('.');
        if (dotPos != std::string::npos) {
            return fileName.substr(0, dotPos);
        }
        return fileName;
    };

    std::vector<std::string> materialsToRemove;

    for (auto &[name, data] : m_renderDataMap) {
        if (!data || !data->material)
            continue;

        // Check if this material uses the specified shader
        const std::string &vertPath = data->material->GetVertexShaderPath();
        const std::string &fragPath = data->material->GetFragmentShaderPath();

        // Check exact match or shader name match
        bool matchesVert = (vertPath == shaderId) || (extractShaderName(vertPath) == shaderId);
        bool matchesFrag = (fragPath == shaderId) || (extractShaderName(fragPath) == shaderId);

        if (matchesVert || matchesFrag) {
            INFLOG_DEBUG("Material '", name, "' uses shader '", shaderId, "', marking for invalidation");
            materialsToRemove.push_back(name);
        }
    }

    // Remove render data for affected materials (force recreation)
    for (const auto &name : materialsToRemove) {
        RemoveRenderData(name);
    }

    INFLOG_INFO("Invalidated ", materialsToRemove.size(), " materials using shader '", shaderId, "'");
}

void MaterialPipelineManager::RemoveRenderData(const std::string &materialName)
{
    auto it = m_renderDataMap.find(materialName);
    if (it == m_renderDataMap.end()) {
        return;
    }

    auto &data = it->second;
    if (data) {
        // Clear material's cached pipeline
        if (data->material) {
            data->material->SetPipeline(VK_NULL_HANDLE);
            data->material->SetPipelineLayout(VK_NULL_HANDLE);
            data->material->SetDescriptorSet(VK_NULL_HANDLE);
        }

        // Remove pipeline from cache if present
        if (data->pipeline != VK_NULL_HANDLE) {
            // Find and remove from pipeline cache
            for (auto pipeIt = m_pipelineCache.begin(); pipeIt != m_pipelineCache.end();) {
                if (pipeIt->second == data->pipeline) {
                    pipeIt = m_pipelineCache.erase(pipeIt);
                } else {
                    ++pipeIt;
                }
            }
            // Destroy the pipeline
            vkDestroyPipeline(m_device, data->pipeline, nullptr);
        }
    }

    m_renderDataMap.erase(it);
    INFLOG_DEBUG("Removed render data for material: ", materialName);
}

} // namespace infengine
