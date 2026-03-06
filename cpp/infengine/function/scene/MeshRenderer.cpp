#include "MeshRenderer.h"
#include "ComponentFactory.h"
#include "GameObject.h"
#include "SceneManager.h"
#include <core/log/InfLog.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace infengine
{

INFENGINE_REGISTER_COMPONENT("MeshRenderer", MeshRenderer)

MeshRenderer::~MeshRenderer()
{
    // Safety net: ensure we're removed from the registry even if
    // OnDisable wasn't called (e.g. direct destruction during scene teardown).
    SceneManager::Instance().UnregisterMeshRenderer(this);
}

void MeshRenderer::OnEnable()
{
    SceneManager::Instance().RegisterMeshRenderer(this);
}

void MeshRenderer::OnDisable()
{
    SceneManager::Instance().UnregisterMeshRenderer(this);
}

void MeshRenderer::ComputeLocalBoundsFromInlineVertices()
{
    if (m_inlineVertices.empty()) {
        m_localBoundsMin = glm::vec3(-0.5f);
        m_localBoundsMax = glm::vec3(0.5f);
        return;
    }

    glm::vec3 bmin(std::numeric_limits<float>::max());
    glm::vec3 bmax(std::numeric_limits<float>::lowest());
    for (const auto &v : m_inlineVertices) {
        bmin = glm::min(bmin, v.pos);
        bmax = glm::max(bmax, v.pos);
    }
    m_localBoundsMin = bmin;
    m_localBoundsMax = bmax;
}

std::shared_ptr<InfMaterial> MeshRenderer::GetEffectiveMaterial() const
{
    if (m_renderMaterial) {
        return m_renderMaterial;
    }
    // Return the default material from MaterialManager
    return MaterialManager::Instance().GetDefaultMaterial();
}

void MeshRenderer::GetWorldBounds(glm::vec3 &outMin, glm::vec3 &outMax) const
{
    if (!m_gameObject) {
        outMin = m_localBoundsMin;
        outMax = m_localBoundsMax;
        return;
    }

    const Transform *transform = m_gameObject->GetTransform();
    glm::mat4 worldMatrix = transform->GetWorldMatrix();

    // Transform all 8 corners of the local AABB
    glm::vec3 corners[8] = {
        glm::vec3(m_localBoundsMin.x, m_localBoundsMin.y, m_localBoundsMin.z),
        glm::vec3(m_localBoundsMax.x, m_localBoundsMin.y, m_localBoundsMin.z),
        glm::vec3(m_localBoundsMin.x, m_localBoundsMax.y, m_localBoundsMin.z),
        glm::vec3(m_localBoundsMax.x, m_localBoundsMax.y, m_localBoundsMin.z),
        glm::vec3(m_localBoundsMin.x, m_localBoundsMin.y, m_localBoundsMax.z),
        glm::vec3(m_localBoundsMax.x, m_localBoundsMin.y, m_localBoundsMax.z),
        glm::vec3(m_localBoundsMin.x, m_localBoundsMax.y, m_localBoundsMax.z),
        glm::vec3(m_localBoundsMax.x, m_localBoundsMax.y, m_localBoundsMax.z),
    };

    outMin = glm::vec3(std::numeric_limits<float>::max());
    outMax = glm::vec3(std::numeric_limits<float>::lowest());

    for (const auto &corner : corners) {
        glm::vec4 worldCorner = worldMatrix * glm::vec4(corner, 1.0f);
        glm::vec3 wc = glm::vec3(worldCorner);

        outMin = glm::min(outMin, wc);
        outMax = glm::max(outMax, wc);
    }
}

std::string MeshRenderer::Serialize() const
{
    json j;
    j["schema_version"] = 1;
    j["type"] = GetTypeName();
    j["enabled"] = IsEnabled();
    j["component_id"] = GetComponentID();

    // Mesh reference
    j["meshId"] = m_mesh.meshId;
    j["meshPath"] = m_mesh.meshPath;

    // Materials (legacy MaterialRef array)
    json materials = json::array();
    for (const auto &mat : m_materials) {
        json matJson;
        matJson["materialId"] = mat.materialId;
        matJson["materialPath"] = mat.materialPath;
        materials.push_back(matJson);
    }
    j["materials"] = materials;

    // Render Material Reference - only store path/name, NOT full material data
    // Material data is stored in .mat files, scene only stores reference
    auto effectiveMaterial = GetEffectiveMaterial();
    if (effectiveMaterial) {
        json renderMatJson;
        renderMatJson["name"] = effectiveMaterial->GetName();
        renderMatJson["materialPath"] = effectiveMaterial->GetFilePath(); // Path to .mat file
        renderMatJson["isDefault"] = (m_renderMaterial == nullptr);
        j["renderMaterial"] = renderMatJson;
    } else {
        j["renderMaterial"] = nullptr;
    }

    // Rendering flags
    j["castShadows"] = m_castShadows;
    j["receivesShadows"] = m_receiveShadows;

    // Bounds
    j["boundsMin"] = {m_localBoundsMin.x, m_localBoundsMin.y, m_localBoundsMin.z};
    j["boundsMax"] = {m_localBoundsMax.x, m_localBoundsMax.y, m_localBoundsMax.z};

    // Inline mesh data (for primitives like cubes)
    j["useInlineMesh"] = m_useInlineMesh;
    if (m_useInlineMesh) {
        json verticesJson = json::array();
        for (const auto &v : m_inlineVertices) {
            json vj;
            vj["pos"] = {v.pos.x, v.pos.y, v.pos.z};
            vj["normal"] = {v.normal.x, v.normal.y, v.normal.z};
            vj["tangent"] = {v.tangent.x, v.tangent.y, v.tangent.z, v.tangent.w};
            vj["color"] = {v.color.x, v.color.y, v.color.z};
            vj["texCoord"] = {v.texCoord.x, v.texCoord.y};
            verticesJson.push_back(vj);
        }
        j["inlineVertices"] = verticesJson;

        json indicesJson = json::array();
        for (uint32_t idx : m_inlineIndices) {
            indicesJson.push_back(idx);
        }
        j["inlineIndices"] = indicesJson;
    }

    return j.dump(2);
}

bool MeshRenderer::Deserialize(const std::string &jsonStr)
{
    try {
        json j = json::parse(jsonStr);

        // Call base class deserialize
        Component::Deserialize(jsonStr);

        // Mesh reference
        if (j.contains("meshId")) {
            m_mesh.meshId = j["meshId"].get<uint64_t>();
        }
        if (j.contains("meshPath")) {
            m_mesh.meshPath = j["meshPath"].get<std::string>();
        }

        // Materials (legacy MaterialRef array)
        if (j.contains("materials") && j["materials"].is_array()) {
            m_materials.clear();
            for (const auto &matJson : j["materials"]) {
                MaterialRef mat;
                if (matJson.contains("materialId")) {
                    mat.materialId = matJson["materialId"].get<uint64_t>();
                }
                if (matJson.contains("materialPath")) {
                    mat.materialPath = matJson["materialPath"].get<std::string>();
                }
                m_materials.push_back(mat);
            }
        }

        // Render Material Reference - load material by name or path
        // Material data is stored in .mat files, not in scene
        if (j.contains("renderMaterial") && !j["renderMaterial"].is_null()) {
            const auto &rmJson = j["renderMaterial"];
            std::string matName = rmJson.value("name", "");
            std::string matPath = rmJson.value("materialPath", "");
            bool isDefault = rmJson.value("isDefault", true);

            if (isDefault || matName == "DefaultUnlit") {
                // Use the default material from MaterialManager
                // The default material will be loaded from project's .mat file if available
                m_renderMaterial = nullptr; // GetEffectiveMaterial() will return default
            } else if (!matPath.empty()) {
                // Try to load material from .mat file path first
                m_renderMaterial = MaterialManager::Instance().LoadMaterial(matPath);
                if (!m_renderMaterial) {
                    INFLOG_WARN("MeshRenderer: Failed to load material from: ", matPath);
                }
            } else if (!matName.empty() && MaterialManager::Instance().HasMaterial(matName)) {
                // Material already loaded in MaterialManager by name
                m_renderMaterial = MaterialManager::Instance().GetMaterial(matName);
            } else {
                m_renderMaterial = nullptr;
            }
        } else {
            m_renderMaterial = nullptr;
        }

        // Rendering flags
        if (j.contains("castShadows")) {
            m_castShadows = j["castShadows"].get<bool>();
        }
        if (j.contains("receivesShadows")) {
            m_receiveShadows = j["receivesShadows"].get<bool>();
        }

        // Bounds
        if (j.contains("boundsMin") && j["boundsMin"].is_array() && j["boundsMin"].size() == 3) {
            m_localBoundsMin.x = j["boundsMin"][0].get<float>();
            m_localBoundsMin.y = j["boundsMin"][1].get<float>();
            m_localBoundsMin.z = j["boundsMin"][2].get<float>();
        }
        if (j.contains("boundsMax") && j["boundsMax"].is_array() && j["boundsMax"].size() == 3) {
            m_localBoundsMax.x = j["boundsMax"][0].get<float>();
            m_localBoundsMax.y = j["boundsMax"][1].get<float>();
            m_localBoundsMax.z = j["boundsMax"][2].get<float>();
        }

        // Inline mesh data (for primitives like cubes)
        m_useInlineMesh = j.value("useInlineMesh", false);
        m_inlineVertices.clear();
        m_inlineIndices.clear();

        if (m_useInlineMesh) {
            if (j.contains("inlineVertices") && j["inlineVertices"].is_array()) {
                for (const auto &vj : j["inlineVertices"]) {
                    Vertex v;
                    if (vj.contains("pos") && vj["pos"].is_array() && vj["pos"].size() == 3) {
                        v.pos.x = vj["pos"][0].get<float>();
                        v.pos.y = vj["pos"][1].get<float>();
                        v.pos.z = vj["pos"][2].get<float>();
                    }
                    if (vj.contains("normal") && vj["normal"].is_array() && vj["normal"].size() == 3) {
                        v.normal.x = vj["normal"][0].get<float>();
                        v.normal.y = vj["normal"][1].get<float>();
                        v.normal.z = vj["normal"][2].get<float>();
                    } else {
                        v.normal = glm::vec3(0.0f, 1.0f, 0.0f); // default up normal for legacy scenes
                    }
                    if (vj.contains("tangent") && vj["tangent"].is_array() && vj["tangent"].size() == 4) {
                        v.tangent.x = vj["tangent"][0].get<float>();
                        v.tangent.y = vj["tangent"][1].get<float>();
                        v.tangent.z = vj["tangent"][2].get<float>();
                        v.tangent.w = vj["tangent"][3].get<float>();
                    } else {
                        v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f); // default tangent for legacy scenes
                    }
                    if (vj.contains("color") && vj["color"].is_array() && vj["color"].size() == 3) {
                        v.color.x = vj["color"][0].get<float>();
                        v.color.y = vj["color"][1].get<float>();
                        v.color.z = vj["color"][2].get<float>();
                    }
                    if (vj.contains("texCoord") && vj["texCoord"].is_array() && vj["texCoord"].size() == 2) {
                        v.texCoord.x = vj["texCoord"][0].get<float>();
                        v.texCoord.y = vj["texCoord"][1].get<float>();
                    }
                    m_inlineVertices.push_back(v);
                }
            }
            if (j.contains("inlineIndices") && j["inlineIndices"].is_array()) {
                for (const auto &idx : j["inlineIndices"]) {
                    m_inlineIndices.push_back(idx.get<uint32_t>());
                }
            }
        }

        return true;
    } catch (const std::exception &e) {
        return false;
    }
}

} // namespace infengine
