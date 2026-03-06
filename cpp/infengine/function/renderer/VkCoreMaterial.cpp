/**
 * @file VkCoreMaterial.cpp
 * @brief InfVkCoreModular — Material system, lighting, and buffer accessors
 *
 * Split from InfVkCoreModular.cpp for maintainability.
 * Contains: UpdateMaterialUBO, EnsureMaterialUBO, CreateBuffer,
 *           InitializeMaterialSystem, RefreshMaterialPipeline,
 *           SetAmbientColor, UpdateLightingUBO,
 *           GetObject*Buffer, GetLegacy*Buffer, GetUniformBuffer, GetShaderModule.
 */

#include "InfError.h"
#include "InfVkCoreModular.h"

#include <function/renderer/shader/ShaderProgram.h>
#include <function/resources/InfMaterial/InfMaterial.h>

#include <glm/glm.hpp>

#include <cstring>

namespace infengine
{

// ============================================================================
// Shader-code lookup helper (resolves path → SPIR-V code from cache)
//
// Handles: exact match, filename-only match, stem-only match (e.g. "123"
// instead of "123.frag").  Used by both InitializeMaterialSystem and
// RefreshMaterialPipeline.
// ============================================================================

static const std::vector<char> *FindShaderCode(const std::unordered_map<std::string, std::vector<char>> &shaderMap,
                                               const std::string &path)
{
    // Try exact match first
    auto it = shaderMap.find(path);
    if (it != shaderMap.end()) {
        return &it->second;
    }

    // Extract filename from path
    size_t lastSlash = path.find_last_of("/\\");
    std::string filename = (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;

    // Try with filename (with extension)
    it = shaderMap.find(filename);
    if (it != shaderMap.end()) {
        return &it->second;
    }

    // Try without extension (shader_id style: "123" instead of "123.frag")
    size_t dotPos = filename.find_last_of('.');
    if (dotPos != std::string::npos) {
        std::string nameWithoutExt = filename.substr(0, dotPos);
        it = shaderMap.find(nameWithoutExt);
        if (it != shaderMap.end()) {
            return &it->second;
        }
    }

    return nullptr;
}

// ============================================================================
// Material UBO Management
// ============================================================================

void InfVkCoreModular::UpdateMaterialUBO(InfMaterial &material)
{
    if (!material.IsPropertiesDirty()) {
        return;
    }

    if (m_materialPipelineManagerInitialized && material.GetShaderProgram() &&
        material.GetDescriptorSet() != VK_NULL_HANDLE) {
        m_materialPipelineManager.UpdateMaterialProperties(material.GetName(), material);
        material.ClearPropertiesDirty();
        return;
    }

    size_t uboSize = InfMaterial::GetUBOSize();

    const auto &properties = material.GetAllProperties();

    std::vector<uint8_t> uboData(uboSize, 0);

    ShaderProgram *shaderProgram = material.GetShaderProgram();
    const MaterialUBOLayout *uboLayout = shaderProgram ? shaderProgram->GetMaterialUBOLayout() : nullptr;

    if (uboLayout && !uboLayout->members.empty()) {
        for (const auto &[name, prop] : properties) {
            uint32_t memberOffset = 0;
            uint32_t memberSize = 0;

            if (!uboLayout->GetMemberInfo(name, memberOffset, memberSize)) {
                continue;
            }

            switch (prop.type) {
            case MaterialPropertyType::Float4: {
                if (memberOffset + sizeof(glm::vec4) <= uboSize) {
                    glm::vec4 value = std::get<glm::vec4>(prop.value);
                    std::memcpy(uboData.data() + memberOffset, &value, sizeof(glm::vec4));
                }
                break;
            }
            case MaterialPropertyType::Float3: {
                if (memberOffset + sizeof(glm::vec3) <= uboSize) {
                    glm::vec3 value = std::get<glm::vec3>(prop.value);
                    std::memcpy(uboData.data() + memberOffset, &value, sizeof(glm::vec3));
                }
                break;
            }
            case MaterialPropertyType::Float2: {
                if (memberOffset + sizeof(glm::vec2) <= uboSize) {
                    glm::vec2 value = std::get<glm::vec2>(prop.value);
                    std::memcpy(uboData.data() + memberOffset, &value, sizeof(glm::vec2));
                }
                break;
            }
            case MaterialPropertyType::Float: {
                if (memberOffset + sizeof(float) <= uboSize) {
                    float value = std::get<float>(prop.value);
                    std::memcpy(uboData.data() + memberOffset, &value, sizeof(float));
                }
                break;
            }
            case MaterialPropertyType::Int: {
                if (memberOffset + sizeof(int) <= uboSize) {
                    int value = std::get<int>(prop.value);
                    std::memcpy(uboData.data() + memberOffset, &value, sizeof(int));
                }
                break;
            }
            default:
                break;
            }
        }
    } else {
        size_t offset = 0;

        for (const auto &[name, prop] : properties) {
            if (prop.type == MaterialPropertyType::Float4) {
                offset = (offset + 15) & ~15;
                if (offset + sizeof(glm::vec4) <= uboSize) {
                    glm::vec4 value = std::get<glm::vec4>(prop.value);
                    std::memcpy(uboData.data() + offset, &value, sizeof(glm::vec4));
                    offset += sizeof(glm::vec4);
                }
            }
        }

        for (const auto &[name, prop] : properties) {
            if (prop.type == MaterialPropertyType::Float3) {
                offset = (offset + 15) & ~15;
                if (offset + sizeof(glm::vec3) <= uboSize) {
                    glm::vec3 value = std::get<glm::vec3>(prop.value);
                    std::memcpy(uboData.data() + offset, &value, sizeof(glm::vec3));
                    offset += 16;
                }
            }
        }

        for (const auto &[name, prop] : properties) {
            if (prop.type == MaterialPropertyType::Float2) {
                offset = (offset + 7) & ~7;
                if (offset + sizeof(glm::vec2) <= uboSize) {
                    glm::vec2 value = std::get<glm::vec2>(prop.value);
                    std::memcpy(uboData.data() + offset, &value, sizeof(glm::vec2));
                    offset += sizeof(glm::vec2);
                }
            }
        }

        for (const auto &[name, prop] : properties) {
            if (prop.type == MaterialPropertyType::Float) {
                offset = (offset + 3) & ~3;
                if (offset + sizeof(float) <= uboSize) {
                    float value = std::get<float>(prop.value);
                    std::memcpy(uboData.data() + offset, &value, sizeof(float));
                    offset += sizeof(float);
                }
            }
        }

        for (const auto &[name, prop] : properties) {
            if (prop.type == MaterialPropertyType::Int) {
                offset = (offset + 3) & ~3;
                if (offset + sizeof(int) <= uboSize) {
                    int value = std::get<int>(prop.value);
                    std::memcpy(uboData.data() + offset, &value, sizeof(int));
                    offset += sizeof(int);
                }
            }
        }
    }

    if (material.HasUBO()) {
        void *matMappedData = material.GetUBOMappedData();
        if (matMappedData) {
            std::memcpy(matMappedData, uboData.data(), uboSize);
        }
    } else {
        for (size_t i = 0; i < m_materialUboMapped.size(); ++i) {
            if (m_materialUboMapped[i]) {
                std::memcpy(m_materialUboMapped[i], uboData.data(), uboSize);
            }
        }
    }

    material.ClearPropertiesDirty();
}

void InfVkCoreModular::EnsureMaterialUBO(std::shared_ptr<InfMaterial> material)
{
    if (!material) {
        return;
    }

    if (material->HasUBO()) {
        return;
    }

    VkBuffer uboBuffer = VK_NULL_HANDLE;
    VmaAllocation uboAllocation = VK_NULL_HANDLE;
    void *uboMappedData = nullptr;

    size_t uboSize = material->GetUBOSize();
    CreateBuffer(uboSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uboBuffer, uboAllocation);

    VmaAllocator allocator = m_deviceContext.GetVmaAllocator();
    vmaMapMemory(allocator, uboAllocation, &uboMappedData);
    if (uboMappedData) {
        std::memset(uboMappedData, 0, uboSize);
    }

    material->SetUBOBuffer(allocator, uboBuffer, uboAllocation, uboMappedData);
}

// ============================================================================
// Material / Pipeline System
// ============================================================================

void InfVkCoreModular::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                                    VkBuffer &buffer, VmaAllocation &allocation)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocator allocator = m_deviceContext.GetVmaAllocator();
    VmaAllocationCreateInfo allocCreateInfo{};

    if (properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
        if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            allocCreateInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        } else {
            allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        }
    } else if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocCreateInfo.requiredFlags = properties;
        if (usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
            allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        } else {
            allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        }
    } else {
        allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
    }

    VkResult result = vmaCreateBuffer(allocator, &bufferInfo, &allocCreateInfo, &buffer, &allocation, nullptr);
    if (result != VK_SUCCESS) {
        INFLOG_ERROR("CreateBuffer: failed to create buffer via VMA");
        buffer = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }
}

void InfVkCoreModular::InitializeMaterialSystem()
{
    if (m_materialSystemInitialized) {
        return;
    }

    MaterialManager::Instance().Initialize();

    if (!m_materialPipelineManagerInitialized) {
        // Use SceneRenderTarget-compatible formats: R8G8B8A8_UNORM color + device depth format
        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        VkFormat depthFormat = m_deviceContext.FindDepthFormat();
        m_materialPipelineManager.Initialize(m_deviceContext.GetVmaAllocator(), GetDevice(), GetPhysicalDevice(),
                                             colorFormat, depthFormat, m_msaaSampleCount, m_shaderProgramCache);
        m_materialPipelineManagerInitialized = true;

        auto defaultTextureIt = m_textures.find("white");
        if (defaultTextureIt != m_textures.end()) {
            m_materialPipelineManager.SetDefaultTexture(defaultTextureIt->second->GetView(),
                                                        defaultTextureIt->second->GetSampler());
        }

        auto defaultNormalIt = m_textures.find("_default_normal");
        if (defaultNormalIt != m_textures.end()) {
            m_materialPipelineManager.SetDefaultNormalTexture(defaultNormalIt->second->GetView(),
                                                              defaultNormalIt->second->GetSampler());
        }

        // Set up texture resolver for material Texture2D properties
        // This callback loads textures from disk on first access and caches them
        m_materialPipelineManager.SetTextureResolver(
            [this](const std::string &texturePath) -> std::pair<VkImageView, VkSampler> {
                // Check cache first (keyed by path)
                auto it = m_textures.find(texturePath);
                if (it != m_textures.end() && it->second) {
                    return {it->second->GetView(), it->second->GetSampler()};
                }

                // Load texture from disk → GPU
                auto texture = m_resourceManager.LoadTexture(texturePath);
                if (!texture) {
                    INFLOG_WARN("TextureResolver: failed to load '", texturePath, "'");
                    return {VK_NULL_HANDLE, VK_NULL_HANDLE};
                }

                VkImageView view = texture->GetView();
                VkSampler sampler = texture->GetSampler();
                m_textures[texturePath] = std::move(texture);
                INFLOG_INFO("TextureResolver: loaded texture '", texturePath, "'");
                return {view, sampler};
            });
    }

    auto defaultMaterial = MaterialManager::Instance().GetDefaultMaterial();
    if (defaultMaterial) {
        const std::string &vertId = defaultMaterial->GetVertexShaderPath();
        const std::string &fragId = defaultMaterial->GetFragmentShaderPath();

        const auto *vertCode = FindShaderCode(m_vertShaderCodes, vertId);
        const auto *fragCode = FindShaderCode(m_fragShaderCodes, fragId);

        if (vertCode && fragCode) {
            VkBuffer lightingBuffer =
                m_lightingUboBuffers.empty() ? VK_NULL_HANDLE : m_lightingUboBuffers[0]->GetBuffer();
            m_materialPipelineManager.GetOrCreateRenderDataWithReflection(
                defaultMaterial, *vertCode, *fragCode, defaultMaterial->GetShaderId(),
                m_uniformBuffers.empty() ? VK_NULL_HANDLE : m_uniformBuffers[0]->GetBuffer(),
                sizeof(UniformBufferObject), lightingBuffer, sizeof(ShaderLightingUBO));
        } else {
            INFLOG_ERROR("InitializeMaterialSystem: SPIR-V shader codes not found for default material "
                         "(vert='",
                         vertId, "', frag='", fragId, "'). Reflection path requires shader code cache.");
        }
    }

    // Pre-build error material pipeline (unlit magenta-black checkerboard).
    // Uses dedicated error/error shaders — self-contained, no material UBO needed.
    // If shaders aren't in cache yet, the lazy build in the draw code will handle it.
    auto errorMaterial = MaterialManager::Instance().GetErrorMaterial();
    if (errorMaterial) {
        const std::string &errVertId = errorMaterial->GetVertexShaderPath();
        const std::string &errFragId = errorMaterial->GetFragmentShaderPath();

        const auto *errVertCode = FindShaderCode(m_vertShaderCodes, errVertId);
        const auto *errFragCode = FindShaderCode(m_fragShaderCodes, errFragId);

        if (errVertCode && errFragCode) {
            VkBuffer lightingBuffer =
                m_lightingUboBuffers.empty() ? VK_NULL_HANDLE : m_lightingUboBuffers[0]->GetBuffer();
            auto *renderData = m_materialPipelineManager.GetOrCreateRenderDataWithReflection(
                errorMaterial, *errVertCode, *errFragCode, errorMaterial->GetShaderId(),
                m_uniformBuffers.empty() ? VK_NULL_HANDLE : m_uniformBuffers[0]->GetBuffer(),
                sizeof(UniformBufferObject), lightingBuffer, sizeof(ShaderLightingUBO));
            if (renderData && renderData->isValid) {
                INFLOG_INFO("Error material pipeline created successfully (shaders: ", errVertId, "/", errFragId, ")");
            } else {
                INFLOG_WARN("InitializeMaterialSystem: error material pipeline deferred to lazy build");
            }
        } else {
            INFLOG_WARN("InitializeMaterialSystem: error shader SPIR-V not yet in cache "
                        "(vert='",
                        errVertId, "', frag='", errFragId, "'), will be built lazily on first use");
        }
    }

    m_materialSystemInitialized = true;
}

void InfVkCoreModular::ReinitializeMaterialPipelines(VkSampleCountFlagBits newSampleCount)
{
    if (!m_materialPipelineManagerInitialized) {
        return;
    }

    INFLOG_INFO("ReinitializeMaterialPipelines: changing MSAA sample count to ", static_cast<int>(newSampleCount));

    // Shutdown existing pipelines (caller must have called WaitIdle already)
    m_materialPipelineManager.Shutdown(/* skipWaitIdle */ true);
    m_materialPipelineManagerInitialized = false;

    // Re-initialize with new sample count
    VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
    VkFormat depthFormat = m_deviceContext.FindDepthFormat();
    m_materialPipelineManager.Initialize(m_deviceContext.GetVmaAllocator(), GetDevice(), GetPhysicalDevice(),
                                         colorFormat, depthFormat, newSampleCount, m_shaderProgramCache);
    m_materialPipelineManagerInitialized = true;

    // Restore default textures
    auto defaultTextureIt = m_textures.find("white");
    if (defaultTextureIt != m_textures.end()) {
        m_materialPipelineManager.SetDefaultTexture(defaultTextureIt->second->GetView(),
                                                    defaultTextureIt->second->GetSampler());
    }
    auto defaultNormalIt = m_textures.find("_default_normal");
    if (defaultNormalIt != m_textures.end()) {
        m_materialPipelineManager.SetDefaultNormalTexture(defaultNormalIt->second->GetView(),
                                                          defaultNormalIt->second->GetSampler());
    }

    // Restore texture resolver
    m_materialPipelineManager.SetTextureResolver(
        [this](const std::string &texturePath) -> std::pair<VkImageView, VkSampler> {
            auto it = m_textures.find(texturePath);
            if (it != m_textures.end() && it->second) {
                return {it->second->GetView(), it->second->GetSampler()};
            }
            auto texture = m_resourceManager.LoadTexture(texturePath);
            if (!texture) {
                return {VK_NULL_HANDLE, VK_NULL_HANDLE};
            }
            VkImageView view = texture->GetView();
            VkSampler sampler = texture->GetSampler();
            m_textures[texturePath] = std::move(texture);
            return {view, sampler};
        });

    INFLOG_INFO("ReinitializeMaterialPipelines: complete — pipelines will be lazily rebuilt on next draw");
}

bool InfVkCoreModular::RefreshMaterialPipeline(std::shared_ptr<InfMaterial> material, const std::string &vertShaderPath,
                                               const std::string &fragShaderPath)
{
    if (!material) {
        return false;
    }

    const auto *vertCode = FindShaderCode(m_vertShaderCodes, vertShaderPath);
    const auto *fragCode = FindShaderCode(m_fragShaderCodes, fragShaderPath);

    if (vertCode && fragCode && m_materialPipelineManagerInitialized) {
        VkBuffer sceneUbo = m_uniformBuffers.empty() ? VK_NULL_HANDLE : m_uniformBuffers[0]->GetBuffer();
        VkDeviceSize sceneUboSize = sizeof(UniformBufferObject);
        VkBuffer lightingUbo = m_lightingUboBuffers.empty() ? VK_NULL_HANDLE : m_lightingUboBuffers[0]->GetBuffer();
        VkDeviceSize lightingUboSize = sizeof(ShaderLightingUBO);
        auto *renderData = m_materialPipelineManager.GetOrCreateRenderDataWithReflection(
            material, *vertCode, *fragCode, material->GetShaderId(), sceneUbo, sceneUboSize, lightingUbo,
            lightingUboSize);
        return renderData && renderData->isValid;
    }

    INFLOG_WARN("RefreshMaterialPipeline: shader codes not found or MPM not initialized for '", material->GetName(),
                "' (vert='", vertShaderPath, "', frag='", fragShaderPath, "')");

    // Dump available shader keys for debugging
    static int dumpCount = 0;
    if (dumpCount++ < 2) {
        std::string vertKeys, fragKeys;
        for (const auto &kv : m_vertShaderCodes)
            vertKeys += " [" + kv.first + "]";
        for (const auto &kv : m_fragShaderCodes)
            fragKeys += " [" + kv.first + "]";
        INFLOG_WARN("  Available vert shaders:", vertKeys);
        INFLOG_WARN("  Available frag shaders:", fragKeys);
        INFLOG_WARN("  MPM initialized: ", m_materialPipelineManagerInitialized ? "true" : "false",
                    ", vertCode found: ", (vertCode ? "yes" : "no"), ", fragCode found: ", (fragCode ? "yes" : "no"));
    }
    return false;
}

// ============================================================================
// Lighting System
// ============================================================================

void InfVkCoreModular::SetAmbientColor(const glm::vec3 &color, float intensity)
{
    m_lightCollector.SetAmbientColor(color, intensity);
    INFLOG_DEBUG("SetAmbientColor: (", color.r, ", ", color.g, ", ", color.b, ") intensity=", intensity);
}

void InfVkCoreModular::UpdateLightingUBO(const glm::vec3 &cameraPosition)
{
    // Delegate to StageLightingUBO — the actual GPU write now happens
    // inline in the command buffer via CmdUpdateLightingUBO().
    StageLightingUBO(cameraPosition);
}

void InfVkCoreModular::StageLightingUBO(const glm::vec3 &cameraPosition)
{
    // Phase 2.1: Sync ambient color from skybox material properties
    auto skyMat = MaterialManager::Instance().GetSkyboxMaterial();
    if (skyMat) {
        const auto *skyTopProp = skyMat->GetProperty("skyTopColor");
        const auto *groundProp = skyMat->GetProperty("groundColor");
        const auto *exposureProp = skyMat->GetProperty("exposure");
        if (skyTopProp && groundProp) {
            glm::vec3 skyTop = glm::vec3(std::get<glm::vec4>(skyTopProp->value));
            glm::vec3 ground = glm::vec3(std::get<glm::vec4>(groundProp->value));
            float exposure = 0.8f;
            if (exposureProp) {
                exposure = std::get<float>(exposureProp->value);
            }
            glm::vec3 ambient = glm::mix(ground, skyTop, 0.5f) * exposure;
            m_lightCollector.SetAmbientColor(ambient, 1.0f);
        }
    }

    // Build the shader-compatible UBO from collected lights
    m_lightCollector.BuildShaderLightingUBO();
    m_stagedLightingUBO = m_lightCollector.GetShaderLightingUBO();
    m_stagedLightingUBO.cameraPos = glm::vec4(cameraPosition, 1.0f);
    m_lightingUBODirty = true;
}

void InfVkCoreModular::CmdUpdateLightingCameraPos(VkCommandBuffer cmdBuf, const glm::vec3 &cameraPos)
{
    if (m_lightingUboBuffers.empty() || !m_lightingUboBuffers[0])
        return;

    VkBuffer buffer = m_lightingUboBuffers[0]->GetBuffer();

    // cameraPos sits at offset 32 in ShaderLightingUBO (after lightCounts + ambientColor).
    constexpr VkDeviceSize cameraPosOffset = offsetof(ShaderLightingUBO, cameraPos);
    glm::vec4 cameraPosVec4(cameraPos, 1.0f);

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    vkCmdUpdateBuffer(cmdBuf, buffer, cameraPosOffset, sizeof(glm::vec4), &cameraPosVec4);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier, 0,
                         nullptr, 0, nullptr);
}

void InfVkCoreModular::CmdUpdateLightingUBO(VkCommandBuffer cmdBuf)
{
    if (!m_lightingUBODirty)
        return;
    if (m_lightingUboBuffers.empty() || !m_lightingUboBuffers[0])
        return;

    VkBuffer buffer = m_lightingUboBuffers[0]->GetBuffer();

    // Barrier: ensure previous shader reads from the lighting UBO are complete
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);

    // Update the lighting UBO inline in the command buffer
    // vkCmdUpdateBuffer has a 65536-byte limit; ShaderLightingUBO is well within that.
    vkCmdUpdateBuffer(cmdBuf, buffer, 0, sizeof(ShaderLightingUBO), &m_stagedLightingUBO);

    // Barrier: ensure write is visible before subsequent shader reads
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &barrier, 0,
                         nullptr, 0, nullptr);

    m_lightingUBODirty = false;
}

// ============================================================================
// Buffer / Shader Accessors (for OutlineRenderer)
// ============================================================================

VkBuffer InfVkCoreModular::GetObjectVertexBuffer(uint64_t objectId) const
{
    auto it = m_perObjectBuffers.find(objectId);
    if (it != m_perObjectBuffers.end() && it->second.vertexBuffer)
        return it->second.vertexBuffer->GetBuffer();
    return VK_NULL_HANDLE;
}

VkBuffer InfVkCoreModular::GetObjectIndexBuffer(uint64_t objectId) const
{
    auto it = m_perObjectBuffers.find(objectId);
    if (it != m_perObjectBuffers.end() && it->second.indexBuffer)
        return it->second.indexBuffer->GetBuffer();
    return VK_NULL_HANDLE;
}

VkBuffer InfVkCoreModular::GetUniformBuffer(size_t index) const
{
    if (index < m_uniformBuffers.size() && m_uniformBuffers[index])
        return m_uniformBuffers[index]->GetBuffer();
    return VK_NULL_HANDLE;
}

VkShaderModule InfVkCoreModular::GetShaderModule(const std::string &name, const std::string &type) const
{
    const auto &map = (type == "vertex") ? m_vertShaders : m_fragShaders;
    auto it = map.find(name);
    if (it != map.end())
        return it->second;
    return VK_NULL_HANDLE;
}

} // namespace infengine
