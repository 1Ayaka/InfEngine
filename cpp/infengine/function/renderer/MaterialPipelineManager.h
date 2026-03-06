#pragma once

#include "MaterialDescriptor.h"
#include "shader/ShaderProgram.h"
#include <function/resources/InfMaterial/InfMaterial.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <vulkan/vulkan.h>

namespace infengine
{

// Forward declaration
class ShaderProgram;

/**
 * @brief MaterialRenderData - Runtime render data for a material
 *
 * Contains the Vulkan resources needed to render with a material.
 */
struct MaterialRenderData
{
    std::shared_ptr<InfMaterial> material;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;
    ShaderProgram *shaderProgram = nullptr;           // Reference to cached shader program
    MaterialDescriptorSet *materialDescSet = nullptr; // Per-material descriptor set
    size_t pipelineHash = 0;
    bool isValid = false;
};

/**
 * @brief MaterialPipelineManager - Manages material-to-pipeline mappings
 *
 * This class handles:
 * - Creating Vulkan pipelines for materials
 * - Caching pipelines by material configuration hash
 * - Shader module management for materials
 * - Descriptor set creation for material properties
 */
class MaterialPipelineManager
{
  public:
    MaterialPipelineManager() = default;
    ~MaterialPipelineManager();

    // Non-copyable
    MaterialPipelineManager(const MaterialPipelineManager &) = delete;
    MaterialPipelineManager &operator=(const MaterialPipelineManager &) = delete;

    /**
     * @brief Initialize the manager
     * @param device Vulkan device
     * @param physicalDevice For memory allocation
     * @param colorFormat Color attachment format for pipeline-compatible render pass
     * @param depthFormat Depth attachment format for pipeline-compatible render pass
     * @param sampleCount MSAA sample count
     * @param shaderProgramCache Externally owned ShaderProgramCache instance
     */
    void Initialize(VmaAllocator allocator, VkDevice device, VkPhysicalDevice physicalDevice, VkFormat colorFormat,
                    VkFormat depthFormat, VkSampleCountFlagBits sampleCount, ShaderProgramCache &shaderProgramCache);

    /**
     * @brief Cleanup all resources
     * @param skipWaitIdle If true, skip vkDeviceWaitIdle (caller already drained GPU)
     */
    void Shutdown(bool skipWaitIdle = false);

    /**
     * @brief Get or create render data for a material (new API with shader reflection)
     *
     * This version uses shader reflection to automatically create descriptor sets
     * and pipeline layouts.
     *
     * @param material The material to get render data for
     * @param vertShaderCode SPIR-V code for vertex shader
     * @param fragShaderCode SPIR-V code for fragment shader
     * @param shaderId Unique shader program identifier
     * @param sceneUBO Scene uniform buffer for binding (binding=0)
     * @param sceneUBOSize Size of scene UBO
     * @param lightingUBO Lighting uniform buffer for binding (binding=1)
     * @param lightingUBOSize Size of lighting UBO
     * @return Pointer to render data, or nullptr on failure
     */
    MaterialRenderData *
    GetOrCreateRenderDataWithReflection(std::shared_ptr<InfMaterial> material, const std::vector<char> &vertShaderCode,
                                        const std::vector<char> &fragShaderCode, const std::string &shaderId,
                                        VkBuffer sceneUBO, VkDeviceSize sceneUBOSize,
                                        VkBuffer lightingUBO = VK_NULL_HANDLE, VkDeviceSize lightingUBOSize = 0);

    /**
     * @brief Get existing render data for a material (doesn't create new)
     */
    MaterialRenderData *GetRenderData(const std::string &materialName);

    /**
     * @brief Check if render data exists for a material
     */
    bool HasRenderData(const std::string &materialName) const;

    /**
     * @brief Get the default material render data
     */
    MaterialRenderData *GetDefaultRenderData();

    /**
     * @brief Get pipeline by hash (for caching)
     */
    VkPipeline GetCachedPipeline(size_t pipelineHash) const;

    /**
     * @brief Update material UBO with current property values
     */
    void UpdateMaterialProperties(const std::string &materialName, const InfMaterial &material);

    /**
     * @brief Bind a texture to a material
     */
    void BindMaterialTexture(const std::string &materialName, uint32_t binding, VkImageView imageView,
                             VkSampler sampler);

    /**
     * @brief Set default texture for fallback
     */
    void SetDefaultTexture(VkImageView imageView, VkSampler sampler);

    /**
     * @brief Set default flat normal map texture for fallback
     */
    void SetDefaultNormalTexture(VkImageView imageView, VkSampler sampler);

    /**
     * @brief Set texture resolver for material Texture2D properties
     *
     * When a material has Texture2D properties, this resolver is called to
     * load the texture file and return VkImageView + VkSampler pairs.
     */
    void SetTextureResolver(TextureResolver resolver)
    {
        m_descriptorManager.SetTextureResolver(std::move(resolver));
    }

    /**
     * @brief Get the material descriptor manager
     */
    MaterialDescriptorManager &GetDescriptorManager()
    {
        return m_descriptorManager;
    }

    /**
     * @brief Invalidate render data for materials using a specific shader
     *
     * This should be called when a shader is hot-reloaded to force pipeline recreation.
     * @param shaderId The shader identifier that was modified
     */
    void InvalidateMaterialsUsingShader(const std::string &shaderId);

    /**
     * @brief Remove render data for a specific material (force recreation)
     */
    void RemoveRenderData(const std::string &materialName);

  private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkRenderPass m_internalRenderPass = VK_NULL_HANDLE; // Internally created compatible render pass
    VkFormat m_colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits m_sampleCount = VK_SAMPLE_COUNT_1_BIT;

    // Injected dependency — owned externally by InfVkCoreModular
    ShaderProgramCache *m_shaderProgramCache = nullptr;

    // Material name -> render data
    std::unordered_map<std::string, std::unique_ptr<MaterialRenderData>> m_renderDataMap;

    // Pipeline hash -> pipeline (for sharing pipelines across materials with same config)
    std::unordered_map<size_t, VkPipeline> m_pipelineCache;

    // Vulkan Pipeline Cache for faster recreation
    VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;

    // Default material render data
    MaterialRenderData *m_defaultRenderData = nullptr;

    // Material descriptor manager for per-material descriptor sets
    MaterialDescriptorManager m_descriptorManager;

    /**
     * @brief Create a shader module from SPIR-V code
     */
    VkShaderModule CreateShaderModule(const std::vector<char> &code);

    /**
     * @brief Create pipeline using shader program (new method)
     */
    VkPipeline CreatePipelineWithProgram(ShaderProgram *program, const RenderState &renderState);

    /**
     * @brief Create internal compatible render pass from stored formats
     */
    void CreateInternalRenderPass();
};

} // namespace infengine
