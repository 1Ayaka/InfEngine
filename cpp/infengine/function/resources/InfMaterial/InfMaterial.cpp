#include "InfMaterial.h"
#include <algorithm>
#include <core/log/InfLog.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <unordered_set>

using json = nlohmann::json;

namespace infengine
{

// ============================================================================
// RenderState Implementation
// ============================================================================

bool RenderState::operator==(const RenderState &other) const
{
    return cullMode == other.cullMode && frontFace == other.frontFace && polygonMode == other.polygonMode &&
           topology == other.topology && depthTestEnable == other.depthTestEnable &&
           depthWriteEnable == other.depthWriteEnable && depthCompareOp == other.depthCompareOp &&
           blendEnable == other.blendEnable && srcColorBlendFactor == other.srcColorBlendFactor &&
           dstColorBlendFactor == other.dstColorBlendFactor && srcAlphaBlendFactor == other.srcAlphaBlendFactor &&
           dstAlphaBlendFactor == other.dstAlphaBlendFactor && colorBlendOp == other.colorBlendOp &&
           alphaBlendOp == other.alphaBlendOp && renderQueue == other.renderQueue;
}

size_t RenderState::Hash() const
{
    size_t hash = 0;
    auto hashCombine = [&hash](size_t value) { hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2); };

    hashCombine(static_cast<size_t>(cullMode));
    hashCombine(static_cast<size_t>(frontFace));
    hashCombine(static_cast<size_t>(polygonMode));
    hashCombine(static_cast<size_t>(topology));
    hashCombine(static_cast<size_t>(depthTestEnable));
    hashCombine(static_cast<size_t>(depthWriteEnable));
    hashCombine(static_cast<size_t>(depthCompareOp));
    hashCombine(static_cast<size_t>(blendEnable));
    hashCombine(static_cast<size_t>(srcColorBlendFactor));
    hashCombine(static_cast<size_t>(dstColorBlendFactor));
    hashCombine(static_cast<size_t>(srcAlphaBlendFactor));
    hashCombine(static_cast<size_t>(dstAlphaBlendFactor));
    hashCombine(static_cast<size_t>(colorBlendOp));
    hashCombine(static_cast<size_t>(alphaBlendOp));
    hashCombine(static_cast<size_t>(renderQueue));

    return hash;
}

// ============================================================================
// InfMaterial Implementation
// ============================================================================

InfMaterial::InfMaterial(const std::string &name) : m_name(name)
{
}

InfMaterial::InfMaterial(const std::string &name, const std::string &vertShaderPath, const std::string &fragShaderPath)
    : m_name(name), m_vertexShaderPath(vertShaderPath), m_fragmentShaderPath(fragShaderPath)
{
}

void InfMaterial::SetFloat(const std::string &name, float value)
{
    m_properties[name] = MaterialProperty{name, MaterialPropertyType::Float, value};
    m_propertiesDirty = true;
}

void InfMaterial::SetVector2(const std::string &name, const glm::vec2 &value)
{
    m_properties[name] = MaterialProperty{name, MaterialPropertyType::Float2, value};
    m_propertiesDirty = true;
}

void InfMaterial::SetVector3(const std::string &name, const glm::vec3 &value)
{
    m_properties[name] = MaterialProperty{name, MaterialPropertyType::Float3, value};
    m_propertiesDirty = true;
}

void InfMaterial::SetVector4(const std::string &name, const glm::vec4 &value)
{
    m_properties[name] = MaterialProperty{name, MaterialPropertyType::Float4, value};
    m_propertiesDirty = true;
}

void InfMaterial::SetColor(const std::string &name, const glm::vec4 &color)
{
    SetVector4(name, color);
}

void InfMaterial::SetInt(const std::string &name, int value)
{
    m_properties[name] = MaterialProperty{name, MaterialPropertyType::Int, value};
    m_propertiesDirty = true;
}

void InfMaterial::SetMatrix(const std::string &name, const glm::mat4 &matrix)
{
    m_properties[name] = MaterialProperty{name, MaterialPropertyType::Mat4, matrix};
    m_propertiesDirty = true;
}

void InfMaterial::SetTexture(const std::string &name, const std::string &texturePath)
{
    m_properties[name] = MaterialProperty{name, MaterialPropertyType::Texture2D, texturePath};
    m_propertiesDirty = true;
}

bool InfMaterial::HasProperty(const std::string &name) const
{
    return m_properties.find(name) != m_properties.end();
}

const MaterialProperty *InfMaterial::GetProperty(const std::string &name) const
{
    auto it = m_properties.find(name);
    if (it != m_properties.end()) {
        return &it->second;
    }
    return nullptr;
}

size_t InfMaterial::GetPipelineHash() const
{
    size_t hash = 0;
    auto hashCombine = [&hash](size_t value) { hash ^= value + 0x9e3779b9 + (hash << 6) + (hash >> 2); };

    // Hash shader paths
    hashCombine(std::hash<std::string>{}(m_vertexShaderPath));
    hashCombine(std::hash<std::string>{}(m_fragmentShaderPath));

    // Hash render state
    hashCombine(m_renderState.Hash());

    return hash;
}

std::string InfMaterial::Serialize() const
{
    json j;
    j["name"] = m_name;
    j["guid"] = m_guid;
    j["builtin"] = m_builtin;

    // Shader paths (our unique vert + frag binding!)
    j["shaders"]["vertex"] = m_vertexShaderPath;
    j["shaders"]["fragment"] = m_fragmentShaderPath;

    // Render state
    json rs;
    rs["cullMode"] = static_cast<int>(m_renderState.cullMode);
    rs["frontFace"] = static_cast<int>(m_renderState.frontFace);
    rs["polygonMode"] = static_cast<int>(m_renderState.polygonMode);
    rs["depthTestEnable"] = m_renderState.depthTestEnable;
    rs["depthWriteEnable"] = m_renderState.depthWriteEnable;
    rs["depthCompareOp"] = static_cast<int>(m_renderState.depthCompareOp);
    rs["blendEnable"] = m_renderState.blendEnable;
    rs["srcColorBlendFactor"] = static_cast<int>(m_renderState.srcColorBlendFactor);
    rs["dstColorBlendFactor"] = static_cast<int>(m_renderState.dstColorBlendFactor);
    rs["colorBlendOp"] = static_cast<int>(m_renderState.colorBlendOp);
    rs["renderQueue"] = m_renderState.renderQueue;
    j["renderState"] = rs;

    // Properties
    json props = json::object();
    for (const auto &[propName, prop] : m_properties) {
        json propJson;
        propJson["type"] = static_cast<int>(prop.type);

        switch (prop.type) {
        case MaterialPropertyType::Float:
            propJson["value"] = std::get<float>(prop.value);
            break;
        case MaterialPropertyType::Float2: {
            auto v = std::get<glm::vec2>(prop.value);
            propJson["value"] = {v.x, v.y};
            break;
        }
        case MaterialPropertyType::Float3: {
            auto v = std::get<glm::vec3>(prop.value);
            propJson["value"] = {v.x, v.y, v.z};
            break;
        }
        case MaterialPropertyType::Float4: {
            auto v = std::get<glm::vec4>(prop.value);
            propJson["value"] = {v.x, v.y, v.z, v.w};
            break;
        }
        case MaterialPropertyType::Int:
            propJson["value"] = std::get<int>(prop.value);
            break;
        case MaterialPropertyType::Mat4: {
            auto m = std::get<glm::mat4>(prop.value);
            json matArr = json::array();
            for (int i = 0; i < 4; i++) {
                for (int k = 0; k < 4; k++) {
                    matArr.push_back(m[i][k]);
                }
            }
            propJson["value"] = matArr;
            break;
        }
        case MaterialPropertyType::Texture2D:
            propJson["value"] = std::get<std::string>(prop.value);
            break;
        }
        props[propName] = propJson;
    }
    j["properties"] = props;

    return j.dump(2);
}

bool InfMaterial::SaveToFile() const
{
    if (m_filePath.empty()) {
        INFLOG_WARN("InfMaterial::SaveToFile: No file path set for material '", m_name, "'");
        return false;
    }
    try {
        std::string jsonStr = Serialize();
        std::ofstream file(m_filePath);
        if (!file.is_open()) {
            INFLOG_ERROR("InfMaterial::SaveToFile: Failed to open file '", m_filePath, "'");
            return false;
        }
        file << jsonStr;
        file.close();
        INFLOG_DEBUG("InfMaterial::SaveToFile: Saved material '", m_name, "' to '", m_filePath, "'");
        return true;
    } catch (const std::exception &e) {
        INFLOG_ERROR("InfMaterial::SaveToFile: Exception - ", e.what());
        return false;
    }
}

bool InfMaterial::SaveToFile(const std::string &path)
{
    try {
        std::string jsonStr = Serialize();
        std::ofstream file(path);
        if (!file.is_open()) {
            INFLOG_ERROR("InfMaterial::SaveToFile: Failed to open file '", path, "'");
            return false;
        }
        file << jsonStr;
        file.close();

        // Update stored file path
        const_cast<InfMaterial *>(this)->m_filePath = path;

        INFLOG_DEBUG("InfMaterial::SaveToFile: Saved material '", m_name, "' to '", path, "'");
        return true;
    } catch (const std::exception &e) {
        INFLOG_ERROR("InfMaterial::SaveToFile: Exception - ", e.what());
        return false;
    }
}

bool InfMaterial::Deserialize(const std::string &jsonStr)
{
    try {
        json j = json::parse(jsonStr);

        if (j.contains("name")) {
            m_name = j["name"].get<std::string>();
        }
        if (j.contains("guid")) {
            m_guid = j["guid"].get<std::string>();
        }
        if (j.contains("builtin")) {
            m_builtin = j["builtin"].get<bool>();
        }

        // Shader paths
        if (j.contains("shaders")) {
            auto &shaders = j["shaders"];
            if (shaders.contains("vertex")) {
                m_vertexShaderPath = shaders["vertex"].get<std::string>();
            }
            if (shaders.contains("fragment")) {
                m_fragmentShaderPath = shaders["fragment"].get<std::string>();
            }
        }

        // Render state
        if (j.contains("renderState")) {
            auto &rs = j["renderState"];
            if (rs.contains("cullMode"))
                m_renderState.cullMode = static_cast<VkCullModeFlags>(rs["cullMode"].get<int>());
            if (rs.contains("frontFace"))
                m_renderState.frontFace = static_cast<VkFrontFace>(rs["frontFace"].get<int>());
            if (rs.contains("polygonMode"))
                m_renderState.polygonMode = static_cast<VkPolygonMode>(rs["polygonMode"].get<int>());
            if (rs.contains("depthTestEnable"))
                m_renderState.depthTestEnable = rs["depthTestEnable"].get<bool>();
            if (rs.contains("depthWriteEnable"))
                m_renderState.depthWriteEnable = rs["depthWriteEnable"].get<bool>();
            if (rs.contains("depthCompareOp"))
                m_renderState.depthCompareOp = static_cast<VkCompareOp>(rs["depthCompareOp"].get<int>());
            if (rs.contains("blendEnable"))
                m_renderState.blendEnable = rs["blendEnable"].get<bool>();
            if (rs.contains("srcColorBlendFactor"))
                m_renderState.srcColorBlendFactor = static_cast<VkBlendFactor>(rs["srcColorBlendFactor"].get<int>());
            if (rs.contains("dstColorBlendFactor"))
                m_renderState.dstColorBlendFactor = static_cast<VkBlendFactor>(rs["dstColorBlendFactor"].get<int>());
            if (rs.contains("colorBlendOp"))
                m_renderState.colorBlendOp = static_cast<VkBlendOp>(rs["colorBlendOp"].get<int>());
            if (rs.contains("renderQueue"))
                m_renderState.renderQueue = rs["renderQueue"].get<int32_t>();
        }

        // Properties
        if (j.contains("properties") && j["properties"].is_object()) {
            m_properties.clear();
            for (auto &[propName, propJson] : j["properties"].items()) {
                MaterialProperty prop;
                prop.name = propName;
                prop.type = static_cast<MaterialPropertyType>(propJson["type"].get<int>());

                switch (prop.type) {
                case MaterialPropertyType::Float:
                    prop.value = propJson["value"].get<float>();
                    break;
                case MaterialPropertyType::Float2:
                    prop.value = glm::vec2(propJson["value"][0].get<float>(), propJson["value"][1].get<float>());
                    break;
                case MaterialPropertyType::Float3:
                    prop.value = glm::vec3(propJson["value"][0].get<float>(), propJson["value"][1].get<float>(),
                                           propJson["value"][2].get<float>());
                    break;
                case MaterialPropertyType::Float4:
                    prop.value = glm::vec4(propJson["value"][0].get<float>(), propJson["value"][1].get<float>(),
                                           propJson["value"][2].get<float>(), propJson["value"][3].get<float>());
                    break;
                case MaterialPropertyType::Int:
                    prop.value = propJson["value"].get<int>();
                    break;
                case MaterialPropertyType::Mat4: {
                    glm::mat4 m;
                    auto &arr = propJson["value"];
                    for (int i = 0; i < 4; i++) {
                        for (int k = 0; k < 4; k++) {
                            m[i][k] = arr[i * 4 + k].get<float>();
                        }
                    }
                    prop.value = m;
                    break;
                }
                case MaterialPropertyType::Texture2D:
                    prop.value = propJson["value"].get<std::string>();
                    break;
                }
                m_properties[propName] = prop;
            }
        }

        m_pipelineDirty = true;
        m_propertiesDirty = true; // Ensure UBO gets updated with loaded values
        return true;
    } catch (const std::exception &e) {
        INFLOG_ERROR("Failed to deserialize material: ", e.what());
        return false;
    }
}

std::shared_ptr<InfMaterial> InfMaterial::CreateDefaultLit()
{
    auto material = std::make_shared<InfMaterial>("DefaultLit");

    // Use lit shader for default material (PBR-inspired Blinn-Phong)
    material->SetVertexShaderPath("lit");
    material->SetFragmentShaderPath("lit");

    // Default lit opaque render state
    RenderState state;
    state.cullMode = VK_CULL_MODE_BACK_BIT;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.depthTestEnable = true;
    state.depthWriteEnable = true;
    state.blendEnable = false;
    state.renderQueue = 2000; // Opaque queue
    material->SetRenderState(state);

    // Default properties from lit shader annotations
    material->SetColor("baseColor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    material->SetFloat("metallic", 0.0f);
    material->SetFloat("roughness", 0.5f);
    material->SetFloat("ambientOcclusion", 1.0f);
    material->SetColor("emissionColor", glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    material->SetFloat("normalScale", 1.0f);
    material->SetFloat("specularHighlights", 1.0f);

    // Mark as built-in (shader cannot be changed by user)
    material->SetBuiltin(true);

    return material;
}

std::shared_ptr<InfMaterial> InfMaterial::CreateDefaultUnlit()
{
    auto material = std::make_shared<InfMaterial>("DefaultUnlit");

    // Use shader_id for lookup (not filename) - shaders are registered by shader_id
    material->SetVertexShaderPath("unlit");
    material->SetFragmentShaderPath("unlit");

    // Default unlit opaque render state
    RenderState state;
    state.cullMode = VK_CULL_MODE_BACK_BIT;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.depthTestEnable = true;
    state.depthWriteEnable = true;
    state.blendEnable = false;
    state.renderQueue = 2000; // Opaque queue
    material->SetRenderState(state);

    // Default property from shader annotation: baseColor
    material->SetColor("baseColor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));

    return material;
}

std::shared_ptr<InfMaterial> InfMaterial::CreateGizmoMaterial()
{
    auto material = std::make_shared<InfMaterial>("GizmoMaterial");

    // Use gizmo shader (simple unlit with vertex color)
    material->SetVertexShaderPath("gizmo");
    material->SetFragmentShaderPath("gizmo");

    // Gizmo render state: no culling (double-sided), depth test, depth write
    RenderState state;
    state.cullMode = VK_CULL_MODE_NONE; // Double-sided for grid visibility
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.depthTestEnable = true;
    state.depthWriteEnable = true;
    state.blendEnable = false;
    state.renderQueue = 20100; // Editor gizmo layer (20001-25000)
    material->SetRenderState(state);

    return material;
}

std::shared_ptr<InfMaterial> InfMaterial::CreateGridMaterial()
{
    auto material = std::make_shared<InfMaterial>("GridMaterial");

    material->SetVertexShaderPath("InfEngine/Grid");
    material->SetFragmentShaderPath("InfEngine/Grid");

    // Grid render state: double-sided, alpha-blended, depth test but no depth write
    RenderState state;
    state.cullMode = VK_CULL_MODE_NONE;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.depthTestEnable = true;
    state.depthWriteEnable = false; // Transparent — don't write depth
    state.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    state.blendEnable = true;
    state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    state.colorBlendOp = VK_BLEND_OP_ADD;
    // Alpha channel: preserve destination alpha (1.0 from opaques/skybox) so
    // the scene texture stays fully opaque when displayed in ImGui viewport.
    state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    state.alphaBlendOp = VK_BLEND_OP_ADD;
    state.renderQueue = 20001; // Editor gizmo layer (20001-25000), renders after all user passes
    material->SetRenderState(state);

    // Default fade distances
    material->SetFloat("fadeStart", 15.0f);
    material->SetFloat("fadeEnd", 80.0f);

    material->SetBuiltin(true);

    return material;
}

std::shared_ptr<InfMaterial> InfMaterial::CreateEditorToolsMaterial()
{
    auto material = std::make_shared<InfMaterial>("EditorToolsMaterial");

    // Same gizmo shader: simple unlit with vertex color
    material->SetVertexShaderPath("gizmo");
    material->SetFragmentShaderPath("gizmo");

    // Editor tools render state: always on top (no depth test), double-sided
    RenderState state;
    state.cullMode = VK_CULL_MODE_NONE;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.depthTestEnable = false;  // Render on top of everything
    state.depthWriteEnable = false; // Don't affect depth buffer
    state.blendEnable = false;
    state.renderQueue = 25001; // Editor tools layer (25001-30000)
    material->SetRenderState(state);

    material->SetBuiltin(true);

    return material;
}

std::shared_ptr<InfMaterial> InfMaterial::CreateComponentGizmosMaterial()
{
    auto material = std::make_shared<InfMaterial>("ComponentGizmosMaterial");

    // Same gizmo shader: simple unlit with vertex color
    material->SetVertexShaderPath("gizmo");
    material->SetFragmentShaderPath("gizmo");

    // Component gizmos: depth-tested (occluded by scene geometry), double-sided, LINE topology
    RenderState state;
    state.cullMode = VK_CULL_MODE_NONE;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    state.depthTestEnable = true;
    state.depthWriteEnable = false; // Don't affect depth buffer
    state.blendEnable = false;
    state.renderQueue = 10000; // Component gizmos layer (10000-20000)
    material->SetRenderState(state);

    material->SetBuiltin(true);

    return material;
}

std::shared_ptr<InfMaterial> InfMaterial::CreateComponentGizmoIconMaterial()
{
    auto material = std::make_shared<InfMaterial>("ComponentGizmoIconMaterial");

    // Same gizmo shader: simple unlit with vertex color
    material->SetVertexShaderPath("gizmo");
    material->SetFragmentShaderPath("gizmo");

    // Icon gizmos: TRIANGLE_LIST for filled diamond billboards,
    // depth-tested (occluded by scene geometry), no depth write, double-sided
    RenderState state;
    state.cullMode = VK_CULL_MODE_NONE;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    state.depthTestEnable = true;
    state.depthWriteEnable = false;
    state.blendEnable = false;
    state.renderQueue = 11000; // After component gizmo lines (10000), same pass
    material->SetRenderState(state);

    material->SetBuiltin(true);

    return material;
}

std::shared_ptr<InfMaterial> InfMaterial::CreateSkyboxProceduralMaterial()
{
    auto material = std::make_shared<InfMaterial>("SkyboxProcedural");

    // Use procedural skybox shader (registered by @shader_id in .vert/.frag)
    material->SetVertexShaderPath("InfEngine/Skybox-Procedural");
    material->SetFragmentShaderPath("InfEngine/Skybox-Procedural");

    // Skybox render state:
    // - Cull back faces (the outside of the cube). Indices are wound CW from
    //   outside, so from inside the camera sees them as CCW (front face with
    //   VK_FRONT_FACE_COUNTER_CLOCKWISE). We keep these front faces by culling
    //   the back (outside-facing) faces.
    // - No depth write (skybox should always be behind everything)
    // - Depth test <= (skybox writes z=1.0, passes only where nothing closer exists)
    // - Render first in the opaque queue (low renderQueue)
    RenderState state;
    state.cullMode = VK_CULL_MODE_BACK_BIT;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.depthTestEnable = true;
    state.depthWriteEnable = false;
    state.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    state.blendEnable = false;
    state.renderQueue = 32767; // After all opaque/transparent, outside shadow caster range
    material->SetRenderState(state);

    // Default sky properties (matching shader @property annotations)
    material->SetColor("skyTopColor", glm::vec4(0.02f, 0.08f, 0.28f, 1.0f));
    material->SetColor("skyHorizonColor", glm::vec4(0.18f, 0.25f, 0.38f, 1.0f));
    material->SetColor("groundColor", glm::vec4(0.08f, 0.07f, 0.12f, 1.0f));
    material->SetFloat("sunSize", 0.04f);
    material->SetFloat("sunIntensity", 1.2f);
    material->SetFloat("exposure", 0.8f);

    material->SetBuiltin(true);

    return material;
}

std::shared_ptr<InfMaterial> InfMaterial::CreateErrorMaterial()
{
    auto material = std::make_shared<InfMaterial>("ErrorMaterial");

    // Use dedicated error shaders: unlit magenta-black checkerboard pattern.
    // These shaders are self-contained (no material UBO, no textures) and
    // output a procedural checkerboard using world-position + UV.
    material->SetVertexShaderPath("error");
    material->SetFragmentShaderPath("error");

    // Double-sided so the error pattern is visible from all angles
    RenderState state;
    state.cullMode = VK_CULL_MODE_NONE;
    state.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    state.depthTestEnable = true;
    state.depthWriteEnable = true;
    state.blendEnable = false;
    state.renderQueue = 2000; // Opaque queue
    material->SetRenderState(state);

    material->SetBuiltin(true);

    return material;
}

// ============================================================================
// MaterialManager Implementation
// ============================================================================

MaterialManager &MaterialManager::Instance()
{
    static MaterialManager instance;
    return instance;
}

void MaterialManager::Initialize()
{
    if (m_initialized) {
        return;
    }

    INFLOG_INFO("Initializing MaterialManager...");

    // Create default material (lit - PBR Blinn-Phong, built-in)
    m_defaultMaterial = InfMaterial::CreateDefaultLit();
    m_materials["DefaultLit"] = m_defaultMaterial;

    // Create gizmo material (for grid and other editor gizmos)
    m_gizmoMaterial = InfMaterial::CreateGizmoMaterial();
    m_gizmoMaterial->SetBuiltin(true);
    m_materials["GizmoMaterial"] = m_gizmoMaterial;

    // Create grid material (distance-fading alpha-blended grid)
    m_gridMaterial = InfMaterial::CreateGridMaterial();
    m_materials["GridMaterial"] = m_gridMaterial;

    // Create component gizmos material (depth-tested user gizmos drawn by components)
    m_componentGizmosMaterial = InfMaterial::CreateComponentGizmosMaterial();
    m_materials["ComponentGizmosMaterial"] = m_componentGizmosMaterial;

    // Create component gizmo icon material (TRIANGLE_LIST billboards for icon clicking)
    m_componentGizmoIconMaterial = InfMaterial::CreateComponentGizmoIconMaterial();
    m_materials["ComponentGizmoIconMaterial"] = m_componentGizmoIconMaterial;

    // Create editor tools material (translate/rotate/scale handles, no depth test)
    m_editorToolsMaterial = InfMaterial::CreateEditorToolsMaterial();
    m_materials["EditorToolsMaterial"] = m_editorToolsMaterial;

    // Create procedural skybox material
    m_skyboxMaterial = InfMaterial::CreateSkyboxProceduralMaterial();
    m_materials["SkyboxProcedural"] = m_skyboxMaterial;

    // Create error material (purple-black checkerboard for shader mismatch)
    m_errorMaterial = InfMaterial::CreateErrorMaterial();
    m_materials["ErrorMaterial"] = m_errorMaterial;

    m_initialized = true;
    INFLOG_INFO("MaterialManager initialized with default lit, gizmo, skybox, and error materials");
}

bool MaterialManager::LoadDefaultMaterialFromFile(const std::string &matFilePath)
{
    if (matFilePath.empty()) {
        return false;
    }

    try {
        std::ifstream file(matFilePath);
        if (!file.is_open()) {
            INFLOG_WARN("MaterialManager: Could not open material file: ", matFilePath);
            return false;
        }

        std::string jsonStr((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        auto material = std::make_shared<InfMaterial>();
        if (!material->Deserialize(jsonStr)) {
            INFLOG_ERROR("MaterialManager: Failed to deserialize material from: ", matFilePath);
            return false;
        }

        // Set the file path so changes can be saved back
        material->SetFilePath(matFilePath);

        // Replace the default material
        m_defaultMaterial = material;
        m_materials["DefaultLit"] = m_defaultMaterial;
        m_materials[material->GetName()] = material;

        INFLOG_INFO("MaterialManager: Loaded default material from: ", matFilePath);
        return true;
    } catch (const std::exception &e) {
        INFLOG_ERROR("MaterialManager: Exception loading material: ", e.what());
        return false;
    }
}

void MaterialManager::Shutdown()
{
    INFLOG_INFO("Shutting down MaterialManager...");
    m_materials.clear();
    m_defaultMaterial.reset();
    m_gizmoMaterial.reset();
    m_gridMaterial.reset();
    m_componentGizmosMaterial.reset();
    m_componentGizmoIconMaterial.reset();
    m_editorToolsMaterial.reset();
    m_skyboxMaterial.reset();
    m_errorMaterial.reset();
    m_initialized = false;
}

std::shared_ptr<InfMaterial> MaterialManager::GetDefaultMaterial()
{
    if (!m_initialized) {
        Initialize();
    }
    return m_defaultMaterial;
}

std::shared_ptr<InfMaterial> MaterialManager::GetGizmoMaterial()
{
    if (!m_initialized) {
        Initialize();
    }
    return m_gizmoMaterial;
}

std::shared_ptr<InfMaterial> MaterialManager::GetGridMaterial()
{
    if (!m_initialized) {
        Initialize();
    }
    return m_gridMaterial;
}

std::shared_ptr<InfMaterial> MaterialManager::GetComponentGizmosMaterial()
{
    if (!m_initialized) {
        Initialize();
    }
    return m_componentGizmosMaterial;
}

std::shared_ptr<InfMaterial> MaterialManager::GetComponentGizmoIconMaterial()
{
    if (!m_initialized) {
        Initialize();
    }
    return m_componentGizmoIconMaterial;
}

std::shared_ptr<InfMaterial> MaterialManager::GetEditorToolsMaterial()
{
    if (!m_initialized) {
        Initialize();
    }
    return m_editorToolsMaterial;
}

std::shared_ptr<InfMaterial> MaterialManager::GetSkyboxMaterial()
{
    if (!m_initialized) {
        Initialize();
    }
    return m_skyboxMaterial;
}

std::shared_ptr<InfMaterial> MaterialManager::GetErrorMaterial()
{
    if (!m_initialized) {
        Initialize();
    }
    return m_errorMaterial;
}

void MaterialManager::RegisterMaterial(const std::string &name, std::shared_ptr<InfMaterial> material)
{
    m_materials[name] = material;
    INFLOG_DEBUG("Registered material: ", name);
}

std::shared_ptr<InfMaterial> MaterialManager::GetMaterial(const std::string &name)
{
    auto it = m_materials.find(name);
    if (it != m_materials.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<InfMaterial>> MaterialManager::GetAllMaterials() const
{
    std::vector<std::shared_ptr<InfMaterial>> result;
    std::unordered_set<InfMaterial *> seen;
    result.reserve(m_materials.size());
    for (const auto &[name, material] : m_materials) {
        if (material && seen.find(material.get()) == seen.end()) {
            result.push_back(material);
            seen.insert(material.get());
        }
    }
    return result;
}

bool MaterialManager::HasMaterial(const std::string &name) const
{
    return m_materials.find(name) != m_materials.end();
}

std::shared_ptr<InfMaterial> MaterialManager::LoadMaterial(const std::string &filePath)
{
    if (filePath.empty()) {
        INFLOG_WARN("MaterialManager::LoadMaterial: empty file path");
        return nullptr;
    }

    // Normalize path separators
    std::string normalizedPath = filePath;
    std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');

    // Check if already loaded
    auto it = m_materials.find(normalizedPath);
    if (it != m_materials.end()) {
        return it->second;
    }

    try {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            INFLOG_WARN("MaterialManager::LoadMaterial: Could not open file: ", filePath);
            return nullptr;
        }

        std::string jsonStr((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        auto material = std::make_shared<InfMaterial>();
        if (!material->Deserialize(jsonStr)) {
            INFLOG_ERROR("MaterialManager::LoadMaterial: Failed to deserialize: ", filePath);
            return nullptr;
        }

        // Set the file path so changes can be saved back
        material->SetFilePath(filePath);

        // Derive material name from filename (without extension)
        std::filesystem::path fsPath(filePath);
        std::string matName = fsPath.stem().string();
        material->SetName(matName);

        // Register the material by both path and name
        m_materials[normalizedPath] = material;
        m_materials[matName] = material;

        INFLOG_INFO("MaterialManager: Loaded material '", matName, "' from: ", filePath);
        return material;
    } catch (const std::exception &e) {
        INFLOG_ERROR("MaterialManager::LoadMaterial: Exception: ", e.what());
        return nullptr;
    }
}

} // namespace infengine
