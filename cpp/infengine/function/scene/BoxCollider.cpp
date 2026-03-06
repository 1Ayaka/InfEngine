/**
 * @file BoxCollider.cpp
 * @brief BoxCollider implementation — Jolt BoxShape creation & serialization.
 */

// Jolt/Jolt.h MUST be the very first include in this TU
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include "BoxCollider.h"
#include "ComponentFactory.h"
#include "GameObject.h"
#include "MeshRenderer.h"
#include "Transform.h"

#include <nlohmann/json.hpp>

namespace infengine
{

INFENGINE_REGISTER_COMPONENT("BoxCollider", BoxCollider)

void BoxCollider::SetSize(const glm::vec3 &size)
{
    m_size = glm::max(size, glm::vec3(0.001f)); // clamp to avoid zero extents
    RebuildShape();
}

void BoxCollider::AutoFitToMesh()
{
    auto *go = GetGameObject();
    if (!go)
        return;
    auto *mr = go->GetComponent<MeshRenderer>();
    if (!mr)
        return;

    glm::vec3 boundsMin = mr->GetLocalBoundsMin();
    glm::vec3 boundsMax = mr->GetLocalBoundsMax();
    glm::vec3 extent = boundsMax - boundsMin;

    m_size = glm::max(extent, glm::vec3(0.001f));
    DataMut().center = (boundsMin + boundsMax) * 0.5f;
}

void *BoxCollider::CreateJoltShapeRaw() const
{
    // Jolt expects half-extents; Unity exposes full size
    glm::vec3 halfExt = m_size * 0.5f * GetWorldScale();
    JPH::Shape *shape = new JPH::BoxShape(JPH::Vec3(halfExt.x, halfExt.y, halfExt.z));

    glm::vec3 center = GetCenter();
    if (center != glm::vec3(0.0f)) {
        shape = new JPH::RotatedTranslatedShape(JPH::Vec3(center.x, center.y, center.z), JPH::Quat::sIdentity(), shape);
    }

    return shape;
}

// ============================================================================
// Serialization
// ============================================================================

std::string BoxCollider::Serialize() const
{
    // Start with base class fields
    auto baseJson = nlohmann::json::parse(Collider::Serialize());
    baseJson["size"] = {m_size.x, m_size.y, m_size.z};
    return baseJson.dump();
}

bool BoxCollider::Deserialize(const std::string &jsonStr)
{
    if (!Collider::Deserialize(jsonStr))
        return false;

    try {
        auto j = nlohmann::json::parse(jsonStr);
        if (j.contains("size")) {
            auto &s = j["size"];
            m_size = glm::vec3(s[0].get<float>(), s[1].get<float>(), s[2].get<float>());
        }
        // Rebuild Jolt shape so Inspector edits take effect immediately.
        // Safe during scene load: RebuildShape() is a no-op when bodyId == 0xFFFFFFFF.
        RebuildShape();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace infengine
