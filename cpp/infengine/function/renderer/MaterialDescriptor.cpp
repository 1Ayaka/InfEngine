#include "MaterialDescriptor.h"
#include <core/log/InfLog.h>
#include <cstring>

namespace infengine
{

// ============================================================================
// MaterialUBO Implementation
// ============================================================================

MaterialUBO::~MaterialUBO()
{
    Destroy();
}

bool MaterialUBO::Create(VmaAllocator allocator, VkDevice device, const MaterialUBOLayout &layout)
{
    m_allocator = allocator;
    m_device = device;
    m_layout = layout;
    m_size = layout.size;

    if (m_size == 0) {
        INFLOG_WARN("Creating MaterialUBO with size 0");
        return true;
    }

    // Create buffer via VMA
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = m_size;
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocCreateInfo{};
    allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    VmaAllocationInfo allocInfo{};
    VkResult result = vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo, &m_buffer, &m_allocation, &allocInfo);
    if (result != VK_SUCCESS) {
        INFLOG_ERROR("Failed to create material UBO buffer via VMA");
        return false;
    }

    m_mappedData = allocInfo.pMappedData;

    // Zero-initialize
    std::memset(m_mappedData, 0, m_size);

    INFLOG_DEBUG("Created MaterialUBO with size ", m_size, " bytes");
    return true;
}

void MaterialUBO::Destroy()
{
    if (m_buffer != VK_NULL_HANDLE && m_allocator != VK_NULL_HANDLE) {
        m_mappedData = nullptr;
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }

    m_allocator = VK_NULL_HANDLE;
    m_device = VK_NULL_HANDLE;
}

void MaterialUBO::Update(const InfMaterial &material)
{
    if (!m_mappedData || m_size == 0) {
        return;
    }

    const auto &properties = material.GetAllProperties();

    for (const auto &[name, prop] : properties) {
        uint32_t offset, size;
        if (!m_layout.GetMemberInfo(name, offset, size)) {
            continue; // Property not in UBO layout
        }

        switch (prop.type) {
        case MaterialPropertyType::Float: {
            float value = std::get<float>(prop.value);
            WriteData(offset, &value, sizeof(float));
            break;
        }
        case MaterialPropertyType::Float2: {
            glm::vec2 value = std::get<glm::vec2>(prop.value);
            WriteData(offset, &value, sizeof(glm::vec2));
            break;
        }
        case MaterialPropertyType::Float3: {
            glm::vec3 value = std::get<glm::vec3>(prop.value);
            WriteData(offset, &value, sizeof(glm::vec3));
            break;
        }
        case MaterialPropertyType::Float4:
        case MaterialPropertyType::Color: {
            glm::vec4 value = std::get<glm::vec4>(prop.value);
            WriteData(offset, &value, sizeof(glm::vec4));
            break;
        }
        case MaterialPropertyType::Int: {
            int value = std::get<int>(prop.value);
            WriteData(offset, &value, sizeof(int));
            break;
        }
        case MaterialPropertyType::Mat4: {
            glm::mat4 value = std::get<glm::mat4>(prop.value);
            WriteData(offset, &value, sizeof(glm::mat4));
            break;
        }
        case MaterialPropertyType::Texture2D:
            // Textures are bound separately, not in UBO
            break;
        }
    }
}

void MaterialUBO::WriteData(uint32_t offset, const void *data, uint32_t size)
{
    if (!m_mappedData || offset + size > m_size) {
        INFLOG_WARN("MaterialUBO write out of bounds: offset=", offset, " size=", size, " bufferSize=", m_size);
        return;
    }

    std::memcpy(static_cast<char *>(m_mappedData) + offset, data, size);
}

void MaterialUBO::SetFloat(const std::string &name, float value)
{
    uint32_t offset, size;
    if (m_layout.GetMemberInfo(name, offset, size)) {
        WriteData(offset, &value, sizeof(float));
    }
}

void MaterialUBO::SetVec2(const std::string &name, const glm::vec2 &value)
{
    uint32_t offset, size;
    if (m_layout.GetMemberInfo(name, offset, size)) {
        WriteData(offset, &value, sizeof(glm::vec2));
    }
}

void MaterialUBO::SetVec3(const std::string &name, const glm::vec3 &value)
{
    uint32_t offset, size;
    if (m_layout.GetMemberInfo(name, offset, size)) {
        WriteData(offset, &value, sizeof(glm::vec3));
    }
}

void MaterialUBO::SetVec4(const std::string &name, const glm::vec4 &value)
{
    uint32_t offset, size;
    if (m_layout.GetMemberInfo(name, offset, size)) {
        WriteData(offset, &value, sizeof(glm::vec4));
    }
}

void MaterialUBO::SetInt(const std::string &name, int value)
{
    uint32_t offset, size;
    if (m_layout.GetMemberInfo(name, offset, size)) {
        WriteData(offset, &value, sizeof(int));
    }
}

void MaterialUBO::SetMat4(const std::string &name, const glm::mat4 &value)
{
    uint32_t offset, size;
    if (m_layout.GetMemberInfo(name, offset, size)) {
        WriteData(offset, &value, sizeof(glm::mat4));
    }
}

// ============================================================================
// MaterialDescriptorManager Implementation
// ============================================================================

MaterialDescriptorManager::~MaterialDescriptorManager()
{
    Shutdown();
}

void MaterialDescriptorManager::Initialize(VmaAllocator allocator, VkDevice device, VkPhysicalDevice physicalDevice,
                                           uint32_t maxMaterials)
{
    m_vmaAllocator = allocator;
    m_device = device;
    m_physicalDevice = physicalDevice;
    m_poolPageSize = maxMaterials;

    if (CreateDescriptorPool(maxMaterials) == VK_NULL_HANDLE) {
        INFLOG_ERROR("Failed to create material descriptor pool");
    }

    INFLOG_INFO("MaterialDescriptorManager initialized with capacity for ", maxMaterials, " materials");
}

void MaterialDescriptorManager::Shutdown()
{
    Clear();

    if (m_device != VK_NULL_HANDLE) {
        for (auto pool : m_descriptorPools) {
            if (pool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(m_device, pool, nullptr);
            }
        }
    }
    m_descriptorPools.clear();

    m_device = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;

    INFLOG_INFO("MaterialDescriptorManager shutdown");
}

VkDescriptorPool MaterialDescriptorManager::CreateDescriptorPool(uint32_t maxMaterials)
{
    // Pool sizes - assume each material may need:
    // - 1 uniform buffer (scene UBO, binding 0)
    // - 1 uniform buffer (lighting UBO, binding 1)
    // - 1 uniform buffer (material UBO, binding 3)
    // - up to 8 combined image samplers (albedo, normal, shadow, etc.)
    std::vector<VkDescriptorPoolSize> poolSizes = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxMaterials * 5},
                                                   {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxMaterials * 8}};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT; // Allow freeing individual sets
    poolInfo.maxSets = maxMaterials;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
        INFLOG_ERROR("Failed to create material descriptor pool");
        return VK_NULL_HANDLE;
    }

    m_descriptorPools.push_back(pool);
    return pool;
}

MaterialDescriptorSet *MaterialDescriptorManager::GetOrCreateDescriptorSet(const InfMaterial &material,
                                                                           const ShaderProgram &program,
                                                                           VkBuffer sceneUBO, VkDeviceSize sceneUBOSize,
                                                                           VkBuffer lightingUBO,
                                                                           VkDeviceSize lightingUBOSize)
{
    const std::string materialName = material.GetMaterialKey();

    VkDescriptorSetLayout requiredLayout = program.GetDescriptorSetLayout(0);
    if (requiredLayout == VK_NULL_HANDLE) {
        INFLOG_ERROR("Shader program has no descriptor set layout");
        return nullptr;
    }

    // Check if already exists AND uses the same layout
    auto it = m_descriptorSets.find(materialName);
    if (it != m_descriptorSets.end() && it->second->isValid) {
        const MaterialUBOLayout *requiredMaterialLayout = program.GetMaterialUBOLayout();
        const MaterialUBOLayout *requiredVertexMaterialLayout = program.GetVertexMaterialUBOLayout();
        bool needsMaterialUBO = requiredMaterialLayout != nullptr && requiredMaterialLayout->size > 0;
        bool needsVertexMaterialUBO = requiredVertexMaterialLayout != nullptr && requiredVertexMaterialLayout->size > 0;
        bool hasMaterialUBO = it->second->materialUBO && it->second->materialUBO->IsValid();
        bool hasVertexMaterialUBO = it->second->vertexMaterialUBO && it->second->vertexMaterialUBO->IsValid();

        // CRITICAL: Must verify layout matches - shader may have changed
        if (it->second->layout == requiredLayout && needsMaterialUBO == hasMaterialUBO &&
            needsVertexMaterialUBO == hasVertexMaterialUBO) {
            INFLOG_DEBUG("GetOrCreateDescriptorSet: REUSING cached descriptor for '", materialName, "'");
            return it->second.get();
        } else {
            INFLOG_INFO("Material '", materialName, "' descriptor requirements changed, recreating descriptor set");
            // Defer destruction of the old descriptor set + UBO.  The GPU
            // may still be referencing them in an in-flight command buffer.
            // Use shared_ptr so the lambda is copy-constructible (std::function requirement).
            auto staleEntry = std::shared_ptr<MaterialDescriptorSet>(std::move(it->second));
            m_descriptorSets.erase(it);
            if (m_deletionQueue) {
                VkDevice dev = m_device;
                m_deletionQueue->Push([dev, entry = std::move(staleEntry)]() {
                    if (entry->descriptorSet != VK_NULL_HANDLE && entry->ownerPool != VK_NULL_HANDLE) {
                        vkFreeDescriptorSets(dev, entry->ownerPool, 1, &entry->descriptorSet);
                    }
                    // MaterialUBO inside entry is destroyed when the shared_ptr dies.
                });
            } else {
                // Fallback: immediate free (caller must ensure GPU idle).
                if (staleEntry->descriptorSet != VK_NULL_HANDLE && staleEntry->ownerPool != VK_NULL_HANDLE) {
                    vkFreeDescriptorSets(m_device, staleEntry->ownerPool, 1, &staleEntry->descriptorSet);
                }
            }
        }
    }

    // Create new descriptor set
    auto matDescSet = std::make_unique<MaterialDescriptorSet>();
    matDescSet->layout = requiredLayout; // Track which layout we're using

    // Allocate descriptor set from the most recent pool; grow if exhausted.
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &requiredLayout;

    VkDescriptorPool activePool = m_descriptorPools.empty() ? VK_NULL_HANDLE : m_descriptorPools.back();
    allocInfo.descriptorPool = activePool;

    VkResult allocResult = (activePool != VK_NULL_HANDLE)
                               ? vkAllocateDescriptorSets(m_device, &allocInfo, &matDescSet->descriptorSet)
                               : VK_ERROR_OUT_OF_POOL_MEMORY;

    if (allocResult != VK_SUCCESS) {
        // Pool exhausted — allocate a new pool page and retry
        INFLOG_WARN("Descriptor pool exhausted, growing pool chain (page ", m_descriptorPools.size(), ")");
        activePool = CreateDescriptorPool(m_poolPageSize);
        if (activePool == VK_NULL_HANDLE) {
            INFLOG_ERROR("Failed to grow descriptor pool for material: ", materialName);
            return nullptr;
        }
        allocInfo.descriptorPool = activePool;
        allocResult = vkAllocateDescriptorSets(m_device, &allocInfo, &matDescSet->descriptorSet);
        if (allocResult != VK_SUCCESS) {
            INFLOG_ERROR("Failed to allocate descriptor set even after pool growth for material: ", materialName);
            return nullptr;
        }
    }
    matDescSet->ownerPool = activePool;

    // Create material UBO if shader has one
    const MaterialUBOLayout *uboLayout = program.GetMaterialUBOLayout();
    if (uboLayout != nullptr && uboLayout->size > 0) {
        matDescSet->materialUBO = std::make_unique<MaterialUBO>();
        if (!matDescSet->materialUBO->Create(m_vmaAllocator, m_device, *uboLayout)) {
            INFLOG_ERROR("Failed to create material UBO for: ", materialName);
        } else {
            // Update UBO with current material values
            matDescSet->materialUBO->Update(material);
        }
    }

    // Create vertex-stage material UBO if the vertex shader declares @property fields (binding 14)
    const MaterialUBOLayout *vertUboLayout = program.GetVertexMaterialUBOLayout();
    if (vertUboLayout != nullptr && vertUboLayout->size > 0) {
        matDescSet->vertexMaterialUBO = std::make_unique<MaterialUBO>();
        if (!matDescSet->vertexMaterialUBO->Create(m_vmaAllocator, m_device, *vertUboLayout)) {
            INFLOG_ERROR("Failed to create vertex material UBO for: ", materialName);
        } else {
            matDescSet->vertexMaterialUBO->Update(material);
        }
    }

    // Update descriptor bindings
    UpdateDescriptorBindings(*matDescSet, program, sceneUBO, sceneUBOSize, lightingUBO, lightingUBOSize);

    // Resolve material Texture2D properties → actual GPU textures
    if (m_textureResolver) {
        const auto &properties = material.GetAllProperties();
        const auto &bindings = program.GetDescriptorBindings();

        // Collect all descriptor writes and flush as a single batch
        std::vector<VkWriteDescriptorSet> texWrites;
        std::vector<VkDescriptorImageInfo> texImageInfos;
        texWrites.reserve(properties.size());
        texImageInfos.reserve(properties.size());

        for (const auto &[propName, prop] : properties) {
            if (prop.type != MaterialPropertyType::Texture2D) {
                continue;
            }

            // Get the texture path from the property value
            const std::string *texturePath = std::get_if<std::string>(&prop.value);
            if (!texturePath || texturePath->empty()) {
                continue;
            }

            // Skip built-in placeholder names — defaults are already applied by UpdateDescriptorBindings
            if (*texturePath == "white" || *texturePath == "black" || *texturePath == "normal") {
                continue;
            }

            // Find the matching sampler binding by name (set 0 only)
            for (const auto &binding : bindings) {
                if (binding.set != 0) {
                    continue;
                }
                if (binding.type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    continue;
                }

                // Match property name to sampler name from shader reflection
                if (binding.name == propName) {
                    auto [imageView, sampler] = m_textureResolver(*texturePath, binding.name);
                    VkImageView finalView = imageView;
                    VkSampler finalSampler = sampler;

                    if (finalView != VK_NULL_HANDLE && finalSampler != VK_NULL_HANDLE) {
                        matDescSet->textureBindings[binding.binding] = {finalView, finalSampler};
                        INFLOG_DEBUG("Bound texture '", *texturePath, "' to binding ", binding.binding,
                                     " for material '", materialName, "'");
                    } else {
                        INFLOG_WARN("Failed to resolve texture '", *texturePath, "' for material '", materialName,
                                    "' property '", propName, "' — binding default texture");
                        bool isNormal = (binding.name.find("normal") != std::string::npos ||
                                         binding.name.find("Normal") != std::string::npos);
                        if (isNormal && m_defaultNormalImageView != VK_NULL_HANDLE) {
                            finalView = m_defaultNormalImageView;
                            finalSampler = m_defaultNormalSampler;
                        } else if (m_defaultImageView != VK_NULL_HANDLE) {
                            finalView = m_defaultImageView;
                            finalSampler = m_defaultSampler;
                        }
                        matDescSet->textureBindings.erase(binding.binding);
                    }

                    if (finalView != VK_NULL_HANDLE && finalSampler != VK_NULL_HANDLE) {
                        texImageInfos.push_back({});
                        auto &imgInfo = texImageInfos.back();
                        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                        imgInfo.imageView = finalView;
                        imgInfo.sampler = finalSampler;

                        texWrites.push_back({});
                        auto &w = texWrites.back();
                        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        w.dstSet = matDescSet->descriptorSet;
                        w.dstBinding = binding.binding;
                        w.dstArrayElement = 0;
                        w.descriptorCount = 1;
                        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                        w.pImageInfo = &texImageInfos.back();
                    }
                    break;
                }
            }
        }

        if (!texWrites.empty()) {
            vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(texWrites.size()), texWrites.data(), 0, nullptr);
        }
    }

    matDescSet->isValid = true;

    MaterialDescriptorSet *result = matDescSet.get();
    m_descriptorSets[materialName] = std::move(matDescSet);

    INFLOG_DEBUG("Created descriptor set for material: ", materialName);
    return result;
}

void MaterialDescriptorManager::UpdateDescriptorBindings(MaterialDescriptorSet &matDescSet,
                                                         const ShaderProgram &program, VkBuffer sceneUBO,
                                                         VkDeviceSize sceneUBOSize, VkBuffer lightingUBO,
                                                         VkDeviceSize lightingUBOSize)
{
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;

    // Reserve space to avoid reallocation invalidating pointers
    const auto &bindings = program.GetDescriptorBindings();
    bufferInfos.reserve(bindings.size());
    imageInfos.reserve(bindings.size());

    INFLOG_DEBUG("UpdateDescriptorBindings: ", bindings.size(),
                 " bindings, lightingUBO=", (lightingUBO != VK_NULL_HANDLE ? "valid" : "null"),
                 ", lightingUBOSize=", lightingUBOSize);

    for (const auto &binding : bindings) {
        // Only write set 0 bindings into the material descriptor set.
        // Set 1 (per-view shadow map) is handled separately per render graph.
        if (binding.set != 0) {
            continue;
        }

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = matDescSet.descriptorSet;
        write.dstBinding = binding.binding;
        write.dstArrayElement = 0;
        write.descriptorCount = binding.descriptorCount;
        write.descriptorType = binding.type;

        if (binding.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            VkDescriptorBufferInfo bufferInfo{};

            INFLOG_DEBUG("  Binding ", binding.binding, ": UBO");

            // Use shader reflection to distinguish material UBO from scene/lighting UBOs.
            // The material UBO binding varies per shader type (e.g. binding 1 for unlit,
            // binding 3 for lit), so we must check reflection rather than hardcoding.
            const MaterialUBOLayout *matLayout = program.GetMaterialUBOLayout();
            bool isMaterialUBOBinding = matLayout && matLayout->size > 0 && binding.binding == matLayout->binding;

            const MaterialUBOLayout *vertMatLayout = program.GetVertexMaterialUBOLayout();
            bool isVertexMaterialUBOBinding =
                vertMatLayout && vertMatLayout->size > 0 && binding.binding == vertMatLayout->binding;

            if (isVertexMaterialUBOBinding && matDescSet.vertexMaterialUBO && matDescSet.vertexMaterialUBO->IsValid()) {
                // Vertex-stage material UBO at binding 14
                bufferInfo.buffer = matDescSet.vertexMaterialUBO->GetBuffer();
                bufferInfo.offset = 0;
                bufferInfo.range = matDescSet.vertexMaterialUBO->GetSize();
            } else if (isMaterialUBOBinding && matDescSet.materialUBO && matDescSet.materialUBO->IsValid()) {
                // Material UBO — identified by shader reflection binding number
                bufferInfo.buffer = matDescSet.materialUBO->GetBuffer();
                bufferInfo.offset = 0;
                bufferInfo.range = matDescSet.materialUBO->GetSize();
            } else if (binding.binding == 0) {
                bufferInfo.buffer = sceneUBO;
                bufferInfo.offset = 0;
                bufferInfo.range = sceneUBOSize;
            } else if (binding.binding == 1 && lightingUBO != VK_NULL_HANDLE) {
                // Lighting UBO at binding 1 (lit shaders only — unlit shaders
                // use binding 1 for MaterialProperties, handled above)
                INFLOG_DEBUG("    -> Binding LightingUBO, size=", lightingUBOSize);
                bufferInfo.buffer = lightingUBO;
                bufferInfo.offset = 0;
                bufferInfo.range = lightingUBOSize;
            } else if (matDescSet.materialUBO && matDescSet.materialUBO->IsValid()) {
                // Fallback: any remaining UBO binding -> material UBO
                bufferInfo.buffer = matDescSet.materialUBO->GetBuffer();
                bufferInfo.offset = 0;
                bufferInfo.range = matDescSet.materialUBO->GetSize();
            } else {
                continue; // Skip if no valid buffer
            }

            bufferInfos.push_back(bufferInfo);
            write.pBufferInfo = &bufferInfos.back();
            writes.push_back(write);
        } else if (binding.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
            VkDescriptorImageInfo imageInfo{};
            imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

            // Check if we have a texture bound for this slot
            auto texIt = matDescSet.textureBindings.find(binding.binding);

            if (texIt != matDescSet.textureBindings.end()) {
                imageInfo.imageView = texIt->second.imageView;
                imageInfo.sampler = texIt->second.sampler;
            } else if (m_defaultNormalImageView != VK_NULL_HANDLE && m_defaultNormalSampler != VK_NULL_HANDLE &&
                       binding.name.find("normal") != std::string::npos) {
                // Use flat normal default for normal-map bindings
                imageInfo.imageView = m_defaultNormalImageView;
                imageInfo.sampler = m_defaultNormalSampler;
            } else if (m_defaultImageView != VK_NULL_HANDLE && m_defaultSampler != VK_NULL_HANDLE) {
                // Use default white texture
                imageInfo.imageView = m_defaultImageView;
                imageInfo.sampler = m_defaultSampler;
            } else {
                continue; // Skip if no valid image
            }

            imageInfos.push_back(imageInfo);
            write.pImageInfo = &imageInfos.back();
            writes.push_back(write);
        }
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void MaterialDescriptorManager::UpdateMaterialUBO(const std::string &materialName, const InfMaterial &material)
{
    auto it = m_descriptorSets.find(materialName);
    if (it != m_descriptorSets.end()) {
        if (it->second->materialUBO) {
            it->second->materialUBO->Update(material);
        }
        if (it->second->vertexMaterialUBO) {
            it->second->vertexMaterialUBO->Update(material);
        }
    }
}

void MaterialDescriptorManager::ResolveTextureProperties(const std::string &materialName, const InfMaterial &material,
                                                         const std::vector<MergedDescriptorBinding> &bindings)
{
    if (!m_textureResolver) {
        return;
    }

    auto it = m_descriptorSets.find(materialName);
    if (it == m_descriptorSets.end() || !it->second->isValid) {
        return;
    }

    auto &matDescSet = *it->second;
    const auto &properties = material.GetAllProperties();

    // Collect all descriptor writes into a single batch
    std::vector<VkWriteDescriptorSet> writes;
    std::vector<VkDescriptorImageInfo> imageInfos;
    writes.reserve(properties.size());
    imageInfos.reserve(properties.size());

    auto queueDefaultForBinding = [&](const MergedDescriptorBinding &binding) {
        VkImageView imageView = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;

        bool isNormal =
            (binding.name.find("normal") != std::string::npos || binding.name.find("Normal") != std::string::npos);
        if (isNormal && m_defaultNormalImageView != VK_NULL_HANDLE && m_defaultNormalSampler != VK_NULL_HANDLE) {
            imageView = m_defaultNormalImageView;
            sampler = m_defaultNormalSampler;
        } else if (m_defaultImageView != VK_NULL_HANDLE && m_defaultSampler != VK_NULL_HANDLE) {
            imageView = m_defaultImageView;
            sampler = m_defaultSampler;
        }

        if (imageView == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE) {
            return;
        }

        matDescSet.textureBindings.erase(binding.binding);

        imageInfos.push_back({});
        auto &imgInfo = imageInfos.back();
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imgInfo.imageView = imageView;
        imgInfo.sampler = sampler;

        writes.push_back({});
        auto &write = writes.back();
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = matDescSet.descriptorSet;
        write.dstBinding = binding.binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfos.back();
    };

    for (const auto &[propName, prop] : properties) {
        if (prop.type != MaterialPropertyType::Texture2D) {
            continue;
        }

        const std::string *texturePath = std::get_if<std::string>(&prop.value);

        for (const auto &binding : bindings) {
            if (binding.set != 0 || binding.type != VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                continue;
            }
            if (binding.name == propName) {
                if (!texturePath || texturePath->empty()) {
                    queueDefaultForBinding(binding);
                    INFLOG_DEBUG("Cleared texture binding ", binding.binding, " for material '", materialName,
                                 "' property '", propName, "' -> rebound default texture");
                    break;
                }

                // Determine if this is a placeholder that should use the default texture
                bool isPlaceholder = (*texturePath == "white" || *texturePath == "black" || *texturePath == "normal");

                VkImageView imageView = VK_NULL_HANDLE;
                VkSampler sampler = VK_NULL_HANDLE;

                if (isPlaceholder) {
                    bool isNormal = (binding.name.find("normal") != std::string::npos ||
                                     binding.name.find("Normal") != std::string::npos);
                    if (isNormal && m_defaultNormalImageView != VK_NULL_HANDLE) {
                        imageView = m_defaultNormalImageView;
                        sampler = m_defaultNormalSampler;
                    } else if (m_defaultImageView != VK_NULL_HANDLE) {
                        imageView = m_defaultImageView;
                        sampler = m_defaultSampler;
                    }
                } else {
                    auto resolved = m_textureResolver(*texturePath, binding.name);
                    imageView = resolved.first;
                    sampler = resolved.second;
                }

                if (imageView != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE) {
                    matDescSet.textureBindings[binding.binding] = {imageView, sampler};

                    imageInfos.push_back({});
                    auto &imgInfo = imageInfos.back();
                    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    imgInfo.imageView = imageView;
                    imgInfo.sampler = sampler;

                    writes.push_back({});
                    auto &write = writes.back();
                    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    write.dstSet = matDescSet.descriptorSet;
                    write.dstBinding = binding.binding;
                    write.dstArrayElement = 0;
                    write.descriptorCount = 1;
                    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    write.pImageInfo = &imageInfos.back();

                    INFLOG_DEBUG("Re-bound texture '", *texturePath, "' to binding ", binding.binding,
                                 " for material '", materialName, "'");
                } else {
                    INFLOG_WARN("Failed to resolve texture '", *texturePath, "' for material '", materialName,
                                "' property '", propName, "' — binding default texture");
                    queueDefaultForBinding(binding);
                }
                break;
            }
        }
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }
}

void MaterialDescriptorManager::BindTexture(const std::string &materialName, uint32_t binding, VkImageView imageView,
                                            VkSampler sampler)
{
    auto it = m_descriptorSets.find(materialName);
    if (it != m_descriptorSets.end()) {
        it->second->textureBindings[binding] = {imageView, sampler};

        // Update the descriptor set immediately
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = imageView;
        imageInfo.sampler = sampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = it->second->descriptorSet;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }
}

void MaterialDescriptorManager::RemoveDescriptorSet(const std::string &materialName)
{
    auto it = m_descriptorSets.find(materialName);
    if (it != m_descriptorSets.end()) {
        // Free the descriptor set back to its owning pool
        if (it->second->descriptorSet != VK_NULL_HANDLE && it->second->ownerPool != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(m_device, it->second->ownerPool, 1, &it->second->descriptorSet);
        }
        m_descriptorSets.erase(it);
    }
}

void MaterialDescriptorManager::Clear()
{
    // Reset all pool pages (more efficient than freeing one by one)
    if (m_device != VK_NULL_HANDLE) {
        for (auto pool : m_descriptorPools) {
            if (pool != VK_NULL_HANDLE) {
                vkResetDescriptorPool(m_device, pool, 0);
            }
        }
    }
    m_descriptorSets.clear();
}

void MaterialDescriptorManager::SetDefaultTexture(VkImageView imageView, VkSampler sampler)
{
    m_defaultImageView = imageView;
    m_defaultSampler = sampler;
}

void MaterialDescriptorManager::SetDefaultNormalTexture(VkImageView imageView, VkSampler sampler)
{
    m_defaultNormalImageView = imageView;
    m_defaultNormalSampler = sampler;
}

} // namespace infengine
