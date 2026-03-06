#pragma once

#include "ShaderReflection.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <vulkan/vulkan.h>

namespace infengine
{

/**
 * @brief Merged descriptor binding info from multiple shader stages
 */
struct MergedDescriptorBinding
{
    uint32_t binding;
    uint32_t set;
    VkDescriptorType type;
    uint32_t descriptorCount;
    VkShaderStageFlags stageFlags; // Combined from all stages using this binding
    std::string name;
};

/**
 * @brief Material uniform buffer layout
 *
 * Describes the layout of a material's UBO that can be
 * updated from material properties.
 */
struct MaterialUBOLayout
{
    uint32_t binding;
    uint32_t size;
    std::vector<UniformMember> members;

    // Get offset and size for a named property
    bool GetMemberInfo(const std::string &name, uint32_t &offset, uint32_t &size) const
    {
        for (const auto &member : members) {
            if (member.name == name) {
                offset = member.offset;
                size = member.size;
                return true;
            }
        }
        return false;
    }
};

/**
 * @brief ShaderProgram - Combined vert + frag shader with merged reflection
 *
 * This class represents a complete shader program (vert + frag) and provides:
 * - Merged descriptor set layouts from both stages
 * - Auto-generated VkDescriptorSetLayout
 * - Material UBO layout extraction
 * - VkPipelineLayout generation
 */
class ShaderProgram
{
  public:
    ShaderProgram() = default;
    ~ShaderProgram();

    // Non-copyable
    ShaderProgram(const ShaderProgram &) = delete;
    ShaderProgram &operator=(const ShaderProgram &) = delete;

    // Movable
    ShaderProgram(ShaderProgram &&other) noexcept;
    ShaderProgram &operator=(ShaderProgram &&other) noexcept;

    /**
     * @brief Create shader program from SPIR-V code
     * @param device Vulkan device
     * @param vertSpirv Vertex shader SPIR-V
     * @param fragSpirv Fragment shader SPIR-V
     * @param shaderId Unique identifier for this program
     * @return true if creation succeeded
     */
    bool Create(VkDevice device, const std::vector<char> &vertSpirv, const std::vector<char> &fragSpirv,
                const std::string &shaderId);

    /**
     * @brief Destroy all Vulkan resources
     */
    void Destroy();

    // Getters
    [[nodiscard]] const std::string &GetShaderId() const
    {
        return m_shaderId;
    }

    [[nodiscard]] VkShaderModule GetVertexModule() const
    {
        return m_vertModule;
    }
    [[nodiscard]] VkShaderModule GetFragmentModule() const
    {
        return m_fragModule;
    }

    [[nodiscard]] VkDescriptorSetLayout GetDescriptorSetLayout(uint32_t set = 0) const
    {
        auto it = m_descriptorSetLayouts.find(set);
        return it != m_descriptorSetLayouts.end() ? it->second : VK_NULL_HANDLE;
    }

    [[nodiscard]] VkPipelineLayout GetPipelineLayout() const
    {
        return m_pipelineLayout;
    }

    [[nodiscard]] const std::vector<MergedDescriptorBinding> &GetDescriptorBindings() const
    {
        return m_descriptorBindings;
    }

    [[nodiscard]] const MaterialUBOLayout *GetMaterialUBOLayout() const
    {
        return m_hasMaterialUBO ? &m_materialUBOLayout : nullptr;
    }

    [[nodiscard]] const ShaderReflection &GetVertexReflection() const
    {
        return m_vertReflection;
    }
    [[nodiscard]] const ShaderReflection &GetFragmentReflection() const
    {
        return m_fragReflection;
    }

    /**
     * @brief Get the number of texture bindings
     */
    [[nodiscard]] uint32_t GetTextureBindingCount() const;

    /**
     * @brief Check if shader has a material properties UBO
     */
    [[nodiscard]] bool HasMaterialUBO() const
    {
        return m_hasMaterialUBO;
    }

    /**
     * @brief Check if valid
     */
    [[nodiscard]] bool IsValid() const
    {
        return m_vertModule != VK_NULL_HANDLE && m_fragModule != VK_NULL_HANDLE;
    }

  private:
    VkDevice m_device = VK_NULL_HANDLE;
    std::string m_shaderId;

    // Shader modules
    VkShaderModule m_vertModule = VK_NULL_HANDLE;
    VkShaderModule m_fragModule = VK_NULL_HANDLE;

    // Reflection data
    ShaderReflection m_vertReflection;
    ShaderReflection m_fragReflection;

    // Merged descriptor bindings
    std::vector<MergedDescriptorBinding> m_descriptorBindings;

    // Vulkan resources (auto-generated from reflection)
    std::unordered_map<uint32_t, VkDescriptorSetLayout> m_descriptorSetLayouts;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;

    // Material UBO layout (if present)
    MaterialUBOLayout m_materialUBOLayout;
    bool m_hasMaterialUBO = false;

    /**
     * @brief Create shader module from SPIR-V
     */
    VkShaderModule CreateShaderModule(const std::vector<char> &code);

    /**
     * @brief Merge reflection data from both stages
     */
    void MergeReflectionData();

    /**
     * @brief Create descriptor set layouts from merged bindings
     */
    bool CreateDescriptorSetLayouts();

    /**
     * @brief Create pipeline layout
     */
    bool CreatePipelineLayout();

    /**
     * @brief Extract material UBO layout (named "MaterialProperties" or binding 2)
     */
    void ExtractMaterialUBOLayout();

    /**
     * @brief Validate vertex output / fragment input interface compatibility
     * @return true if interfaces are compatible, false if there is a type mismatch
     */
    bool ValidateStageInterface() const;
};

/**
 * @brief ShaderProgramCache - Cache for shader programs
 *
 * Manages shader program creation and caching by shader ID.
 */
class ShaderProgramCache
{
  public:
    ShaderProgramCache() = default;
    ~ShaderProgramCache() = default;

    // Non-copyable
    ShaderProgramCache(const ShaderProgramCache &) = delete;
    ShaderProgramCache &operator=(const ShaderProgramCache &) = delete;

    /**
     * @brief Initialize the cache
     */
    void Initialize(VkDevice device);

    /**
     * @brief Shutdown and cleanup all programs
     */
    void Shutdown();

    /**
     * @brief Get or create a shader program
     */
    ShaderProgram *GetOrCreateProgram(const std::string &shaderId, const std::vector<char> &vertSpirv,
                                      const std::vector<char> &fragSpirv);

    /**
     * @brief Get existing program
     */
    ShaderProgram *GetProgram(const std::string &shaderId);

    /**
     * @brief Check if program exists
     */
    bool HasProgram(const std::string &shaderId) const;

    /**
     * @brief Remove a program from cache
     */
    void RemoveProgram(const std::string &shaderId);

    /**
     * @brief Remove all programs containing the specified shader name
     * @param shaderName Simple shader name (e.g., "123", not full path)
     */
    void RemoveProgramsContainingShader(const std::string &shaderName);

    /**
     * @brief Clear all cached programs
     */
    void Clear();

  private:
    VkDevice m_device = VK_NULL_HANDLE;
    std::unordered_map<std::string, std::unique_ptr<ShaderProgram>> m_programs;
    std::unordered_set<std::string> m_failedPrograms; // Shader IDs that failed creation
};

} // namespace infengine
