/**
 * @file SphereCollider.cpp
 * @brief SphereCollider implementation — Jolt SphereShape creation & serialization.
 */

// Jolt/Jolt.h MUST be the very first include in this TU
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include "ComponentFactory.h"
#include "GameObject.h"
#include "MeshRenderer.h"
#include "SphereCollider.h"
#include "Transform.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace infengine
{

INFENGINE_REGISTER_COMPONENT("SphereCollider", SphereCollider)

void SphereCollider::SetRadius(float radius)
{
    m_radius = std::max(radius, 0.001f);
    RebuildShape();
}

void SphereCollider::AutoFitToMesh()
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

    DataMut().center = (boundsMin + boundsMax) * 0.5f;
    m_radius = std::max({extent.x, extent.y, extent.z}) * 0.5f;
    m_radius = std::max(m_radius, 0.001f);
}

void *SphereCollider::CreateJoltShapeRaw() const
{
    glm::vec3 s = GetWorldScale();
    float r = m_radius * std::max({s.x, s.y, s.z});
    JPH::Shape *shape = new JPH::SphereShape(r);

    glm::vec3 center = GetCenter();
    if (center != glm::vec3(0.0f)) {
        shape = new JPH::RotatedTranslatedShape(JPH::Vec3(center.x, center.y, center.z), JPH::Quat::sIdentity(), shape);
    }

    return shape;
}

// ============================================================================
// Serialization
// ============================================================================

std::string SphereCollider::Serialize() const
{
    auto baseJson = nlohmann::json::parse(Collider::Serialize());
    baseJson["radius"] = m_radius;
    return baseJson.dump();
}

bool SphereCollider::Deserialize(const std::string &jsonStr)
{
    if (!Collider::Deserialize(jsonStr))
        return false;

    try {
        auto j = nlohmann::json::parse(jsonStr);
        if (j.contains("radius"))
            m_radius = j["radius"].get<float>();
        RebuildShape();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace infengine
