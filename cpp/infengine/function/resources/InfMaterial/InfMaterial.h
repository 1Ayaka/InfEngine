#pragma once

#include <core/types/InfFwdType.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

namespace infengine
{

// Forward declarations
class InfResource;
class ShaderProgram;
struct MaterialUBOLayout;

/**
 * @brief Shader stage type for the material system
 */
enum class ShaderStageType
{
    Vertex,
    Fragment,
    Geometry,
    TessControl,
    TessEval,
    Compute
};

/**
 * @brief Render state configuration for materials
 *
 * This defines how the GPU should render geometry with this material.
 */
struct RenderState
{
    // Rasterization
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    VkFrontFace frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
    float lineWidth = 1.0f;

    // Primitive topology (default: triangle list)
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Depth/Stencil
    bool depthTestEnable = true;
    bool depthWriteEnable = true;
    VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS;
    bool stencilTestEnable = false;

    // Blending
    bool blendEnable = false;
    VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
    VkBlendFactor srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    VkBlendOp alphaBlendOp = VK_BLEND_OP_ADD;

    // Render queue (for sorting)
    int32_t renderQueue = 2000; // 2000 = Opaque, 3000 = Transparent

    bool operator==(const RenderState &other) const;
    size_t Hash() const;
};

/**
 * @brief Material property types
 */
enum class MaterialPropertyType
{
    Float,
    Float2,
    Float3,
    Float4,
    Int,
    Mat4,
    Texture2D
};

/**
 * @brief A single material property value
 */
using MaterialPropertyValue = std::variant<float, glm::vec2, glm::vec3, glm::vec4, int, glm::mat4, std::string>;

/**
 * @brief Material property descriptor
 */
struct MaterialProperty
{
    std::string name;
    MaterialPropertyType type;
    MaterialPropertyValue value;
};

/**
 * @brief InfMaterial - Material definition for rendering
 *
 * A material in InfEngine consists of:
 * - A vertex shader (.vert file path)
 * - A fragment shader (.frag file path)
 * - Render state configuration
 * - Material properties (uniforms, textures)
 *
 * Unlike traditional engines where a single "shader" is bound,
 * InfEngine explicitly binds vert + frag separately, matching
 * the Vulkan pipeline model.
 */
class InfMaterial
{
  public:
    InfMaterial() = default;
    InfMaterial(const std::string &name);
    InfMaterial(const std::string &name, const std::string &vertShaderPath, const std::string &fragShaderPath);
    ~InfMaterial() = default;

    // Copy/Move
    InfMaterial(const InfMaterial &) = default;
    InfMaterial &operator=(const InfMaterial &) = default;
    InfMaterial(InfMaterial &&) = default;
    InfMaterial &operator=(InfMaterial &&) = default;

    // ========================================================================
    // Identity
    // ========================================================================

    [[nodiscard]] const std::string &GetName() const
    {
        return m_name;
    }
    void SetName(const std::string &name)
    {
        m_name = name;
    }

    [[nodiscard]] const std::string &GetGuid() const
    {
        return m_guid;
    }
    void SetGuid(const std::string &guid)
    {
        m_guid = guid;
    }

    [[nodiscard]] const std::string &GetFilePath() const
    {
        return m_filePath;
    }
    void SetFilePath(const std::string &path)
    {
        m_filePath = path;
    }

    // ========================================================================
    // Built-in flag (built-in materials cannot have their shader changed)
    // ========================================================================

    [[nodiscard]] bool IsBuiltin() const
    {
        return m_builtin;
    }
    void SetBuiltin(bool builtin)
    {
        m_builtin = builtin;
    }

    /// @brief Save material to its file path (if set)
    bool SaveToFile() const;

    /// @brief Save material to specified file path
    bool SaveToFile(const std::string &path);

    // ========================================================================
    // Shader bindings (vert + frag separate!)
    // ========================================================================

    [[nodiscard]] const std::string &GetVertexShaderPath() const
    {
        return m_vertexShaderPath;
    }
    void SetVertexShaderPath(const std::string &path)
    {
        m_vertexShaderPath = path;
        m_pipelineDirty = true;
    }

    [[nodiscard]] const std::string &GetFragmentShaderPath() const
    {
        return m_fragmentShaderPath;
    }
    void SetFragmentShaderPath(const std::string &path)
    {
        m_fragmentShaderPath = path;
        m_pipelineDirty = true;
    }

    /// @brief Set both shaders at once
    void SetShaders(const std::string &vertPath, const std::string &fragPath)
    {
        m_vertexShaderPath = vertPath;
        m_fragmentShaderPath = fragPath;
        m_pipelineDirty = true;
    }

    // ========================================================================
    // Render State
    // ========================================================================

    [[nodiscard]] const RenderState &GetRenderState() const
    {
        return m_renderState;
    }
    void SetRenderState(const RenderState &state)
    {
        m_renderState = state;
        m_pipelineDirty = true;
    }

    [[nodiscard]] int32_t GetRenderQueue() const
    {
        return m_renderState.renderQueue;
    }
    void SetRenderQueue(int32_t queue)
    {
        m_renderState.renderQueue = queue;
    }

    // ========================================================================
    // Material Properties
    // ========================================================================

    void SetFloat(const std::string &name, float value);
    void SetVector2(const std::string &name, const glm::vec2 &value);
    void SetVector3(const std::string &name, const glm::vec3 &value);
    void SetVector4(const std::string &name, const glm::vec4 &value);
    void SetColor(const std::string &name, const glm::vec4 &color);
    void SetInt(const std::string &name, int value);
    void SetMatrix(const std::string &name, const glm::mat4 &matrix);
    void SetTexture(const std::string &name, const std::string &texturePath);

    [[nodiscard]] bool HasProperty(const std::string &name) const;
    [[nodiscard]] const MaterialProperty *GetProperty(const std::string &name) const;
    [[nodiscard]] const std::unordered_map<std::string, MaterialProperty> &GetAllProperties() const
    {
        return m_properties;
    }

    // ========================================================================
    // Pipeline State
    // ========================================================================

    [[nodiscard]] bool IsPipelineDirty() const
    {
        return m_pipelineDirty;
    }
    void ClearPipelineDirty()
    {
        m_pipelineDirty = false;
    }

    /// @brief Get a unique hash for this material's pipeline configuration
    [[nodiscard]] size_t GetPipelineHash() const;

    // ========================================================================
    // ShaderProgram integration (reflection-based UBO layout)
    // ========================================================================

    /// @brief Set the shader program for this material (provides reflection info)
    void SetShaderProgram(ShaderProgram *program)
    {
        m_shaderProgram = program;
    }

    /// @brief Get the shader program
    [[nodiscard]] ShaderProgram *GetShaderProgram() const
    {
        return m_shaderProgram;
    }

    /// @brief Get unique shader ID (vert+frag paths combined)
    [[nodiscard]] std::string GetShaderId() const
    {
        return m_vertexShaderPath + "|" + m_fragmentShaderPath;
    }

    // ========================================================================
    // Vulkan Resources (managed by renderer)
    // ========================================================================

    void SetPipeline(VkPipeline pipeline)
    {
        m_pipeline = pipeline;
    }
    [[nodiscard]] VkPipeline GetPipeline() const
    {
        return m_pipeline;
    }

    void SetPipelineLayout(VkPipelineLayout layout)
    {
        m_pipelineLayout = layout;
    }
    [[nodiscard]] VkPipelineLayout GetPipelineLayout() const
    {
        return m_pipelineLayout;
    }

    void SetDescriptorSet(VkDescriptorSet set)
    {
        m_descriptorSet = set;
    }
    [[nodiscard]] VkDescriptorSet GetDescriptorSet() const
    {
        return m_descriptorSet;
    }

    // ========================================================================
    // Serialization
    // ========================================================================

    [[nodiscard]] std::string Serialize() const;
    bool Deserialize(const std::string &jsonStr);

    /// @brief Create a default lit opaque material (engine built-in)
    static std::shared_ptr<InfMaterial> CreateDefaultLit();

    /// @brief Create a default unlit opaque material
    static std::shared_ptr<InfMaterial> CreateDefaultUnlit();

    /// @brief Create a gizmo material (uses gizmo shader, unlit, no depth write)
    static std::shared_ptr<InfMaterial> CreateGizmoMaterial();

    /// @brief Create a grid material (distance-fading alpha-blended grid)
    static std::shared_ptr<InfMaterial> CreateGridMaterial();

    /// @brief Create the editor tools material (translate/rotate/scale handles, no depth test)
    static std::shared_ptr<InfMaterial> CreateEditorToolsMaterial();

    /// @brief Create the component gizmos material (Python-driven, depth-tested, queue 30000)
    static std::shared_ptr<InfMaterial> CreateComponentGizmosMaterial();

    /// @brief Create the component gizmo icon material (TRIANGLE_LIST billboards, queue 31000)
    static std::shared_ptr<InfMaterial> CreateComponentGizmoIconMaterial();

    /// @brief Create a procedural skybox material (gradient sky + sun)
    static std::shared_ptr<InfMaterial> CreateSkyboxProceduralMaterial();

    /// @brief Create the error material (purple-black checkerboard for shader mismatch)
    static std::shared_ptr<InfMaterial> CreateErrorMaterial();

  private:
    std::string m_name;
    std::string m_guid;
    std::string m_filePath; // File path for saving
    bool m_builtin = false; // Built-in materials cannot have shader changed

    // Shader paths (separate vert + frag - our core design!)
    std::string m_vertexShaderPath;
    std::string m_fragmentShaderPath;

    // Render state
    RenderState m_renderState;

    // Material properties
    std::unordered_map<std::string, MaterialProperty> m_properties;

    // Vulkan resources (set by renderer) - main pass resources
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    // ShaderProgram for reflection-based UBO layout
    ShaderProgram *m_shaderProgram = nullptr;

    // Per-material UBO (Unity-style: each material has its own buffer)
    VkBuffer m_uboBuffer = VK_NULL_HANDLE;
    VmaAllocator m_uboAllocator = VK_NULL_HANDLE;
    VmaAllocation m_uboAllocation = VK_NULL_HANDLE;
    void *m_uboMappedData = nullptr;
    static constexpr size_t c_uboSize = 256; // Max UBO size for material properties

    // Dirty flag for pipeline recreation
    bool m_pipelineDirty = true;

    // Dirty flag for properties (UBO needs update)
    bool m_propertiesDirty = true;

  public:
    // ========================================================================
    // Per-Material UBO (Unity-style)
    // ========================================================================

    void SetUBOBuffer(VmaAllocator allocator, VkBuffer buffer, VmaAllocation allocation, void *mappedData)
    {
        m_uboAllocator = allocator;
        m_uboBuffer = buffer;
        m_uboAllocation = allocation;
        m_uboMappedData = mappedData;
    }

    /// @brief Cleanup UBO resources (call before material destruction)
    void CleanupUBO(VkDevice device)
    {
        if (m_uboBuffer != VK_NULL_HANDLE && m_uboAllocator != VK_NULL_HANDLE) {
            m_uboMappedData = nullptr;
            vmaDestroyBuffer(m_uboAllocator, m_uboBuffer, m_uboAllocation);
            m_uboBuffer = VK_NULL_HANDLE;
            m_uboAllocation = VK_NULL_HANDLE;
        }
    }

    [[nodiscard]] VkBuffer GetUBOBuffer() const
    {
        return m_uboBuffer;
    }
    [[nodiscard]] VmaAllocation GetUBOAllocation() const
    {
        return m_uboAllocation;
    }
    [[nodiscard]] void *GetUBOMappedData() const
    {
        return m_uboMappedData;
    }
    [[nodiscard]] bool HasUBO() const
    {
        return m_uboBuffer != VK_NULL_HANDLE;
    }
    [[nodiscard]] static constexpr size_t GetUBOSize()
    {
        return c_uboSize;
    }

    // ========================================================================
    // Properties Dirty Flag (for UBO sync optimization)
    // ========================================================================

    [[nodiscard]] bool IsPropertiesDirty() const
    {
        return m_propertiesDirty;
    }
    void ClearPropertiesDirty()
    {
        m_propertiesDirty = false;
    }
    void MarkPropertiesDirty()
    {
        m_propertiesDirty = true;
    }
};

/**
 * @brief MaterialManager - Central registry for all materials
 *
 * Manages material loading, caching, and provides the default material.
 */
class MaterialManager
{
  public:
    static MaterialManager &Instance();

    // Non-copyable
    MaterialManager(const MaterialManager &) = delete;
    MaterialManager &operator=(const MaterialManager &) = delete;

    /// @brief Initialize with default materials
    void Initialize();

    /// @brief Cleanup all materials
    void Shutdown();

    /// @brief Get the default material (unlit opaque)
    [[nodiscard]] std::shared_ptr<InfMaterial> GetDefaultMaterial();

    /// @brief Get the gizmo material (for grid and other gizmos)
    [[nodiscard]] std::shared_ptr<InfMaterial> GetGizmoMaterial();

    /// @brief Get the grid material (distance-fading grid)
    [[nodiscard]] std::shared_ptr<InfMaterial> GetGridMaterial();

    /// @brief Get the editor tools material (translate/rotate/scale handles)
    [[nodiscard]] std::shared_ptr<InfMaterial> GetEditorToolsMaterial();

    /// @brief Get the component gizmos material (Python-driven, depth-tested)
    [[nodiscard]] std::shared_ptr<InfMaterial> GetComponentGizmosMaterial();

    /// @brief Get the component gizmo icon material (TRIANGLE_LIST billboards)
    [[nodiscard]] std::shared_ptr<InfMaterial> GetComponentGizmoIconMaterial();

    /// @brief Get the skybox material
    [[nodiscard]] std::shared_ptr<InfMaterial> GetSkyboxMaterial();

    /// @brief Get the error material (purple-black checkerboard for shader mismatch)
    [[nodiscard]] std::shared_ptr<InfMaterial> GetErrorMaterial();

    /// @brief Register a material
    void RegisterMaterial(const std::string &name, std::shared_ptr<InfMaterial> material);

    /// @brief Get a material by name
    [[nodiscard]] std::shared_ptr<InfMaterial> GetMaterial(const std::string &name);

    /// @brief Check if material exists
    [[nodiscard]] bool HasMaterial(const std::string &name) const;

    /// @brief Load material from file path
    std::shared_ptr<InfMaterial> LoadMaterial(const std::string &filePath);

    /// @brief Load default material from a .mat file in project directory
    /// This replaces the hardcoded default material with one from a file
    bool LoadDefaultMaterialFromFile(const std::string &matFilePath);

    /// @brief Get all registered materials
    [[nodiscard]] std::vector<std::shared_ptr<InfMaterial>> GetAllMaterials() const;

  private:
    MaterialManager() = default;
    ~MaterialManager() = default;

    std::unordered_map<std::string, std::shared_ptr<InfMaterial>> m_materials;
    std::shared_ptr<InfMaterial> m_defaultMaterial;
    std::shared_ptr<InfMaterial> m_gizmoMaterial;
    std::shared_ptr<InfMaterial> m_gridMaterial;
    std::shared_ptr<InfMaterial> m_editorToolsMaterial;
    std::shared_ptr<InfMaterial> m_componentGizmosMaterial;
    std::shared_ptr<InfMaterial> m_componentGizmoIconMaterial;
    std::shared_ptr<InfMaterial> m_skyboxMaterial;
    std::shared_ptr<InfMaterial> m_errorMaterial;
    bool m_initialized = false;
};

} // namespace infengine
