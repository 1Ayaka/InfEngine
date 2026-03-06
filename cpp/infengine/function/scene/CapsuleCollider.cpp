/**
 * @file CapsuleCollider.cpp
 * @brief CapsuleCollider implementation — Jolt CapsuleShape creation & serialization.
 */

// Jolt/Jolt.h MUST be the very first include in this TU
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include "CapsuleCollider.h"
#include "ComponentFactory.h"
#include "GameObject.h"
#include "MeshRenderer.h"
#include "Transform.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace infengine
{

INFENGINE_REGISTER_COMPONENT("CapsuleCollider", CapsuleCollider)

void CapsuleCollider::SetRadius(float radius)
{
    m_radius = std::max(radius, 0.001f);
    RebuildShape();
}

void CapsuleCollider::SetHeight(float height)
{
    m_height = std::max(height, m_radius * 2.0f + 0.001f);
    RebuildShape();
}

void CapsuleCollider::SetDirection(int dir)
{
    m_direction = std::clamp(dir, 0, 2);
    RebuildShape();
}

void CapsuleCollider::AutoFitToMesh()
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

    // Choose direction from longest axis (Unity behaviour)
    if (extent.x >= extent.y && extent.x >= extent.z) {
        m_direction = 0; // X
        m_height = std::max(extent.x, 0.001f);
        m_radius = std::max(std::max(extent.y, extent.z) * 0.5f, 0.001f);
    } else if (extent.y >= extent.x && extent.y >= extent.z) {
        m_direction = 1; // Y
        m_height = std::max(extent.y, 0.001f);
        m_radius = std::max(std::max(extent.x, extent.z) * 0.5f, 0.001f);
    } else {
        m_direction = 2; // Z
        m_height = std::max(extent.z, 0.001f);
        m_radius = std::max(std::max(extent.x, extent.y) * 0.5f, 0.001f);
    }

    // Capsule constraint: height >= 2 * radius
    if (m_height < m_radius * 2.0f) {
        m_height = m_radius * 2.0f + 0.001f;
    }
}

void *CapsuleCollider::CreateJoltShapeRaw() const
{
    float r = m_radius;
    float h = m_height;

    // Account for scale
    {
        glm::vec3 s = GetWorldScale();
        float axisScale = (m_direction == 0) ? s.x : (m_direction == 1) ? s.y : s.z;
        float radScale = 0.0f;
        switch (m_direction) {
        case 0:
            radScale = std::max(s.y, s.z);
            break;
        case 1:
            radScale = std::max(s.x, s.z);
            break;
        case 2:
            radScale = std::max(s.x, s.y);
            break;
        }
        h *= axisScale;
        r *= radScale;
    }

    // Jolt CapsuleShape(halfHeight, radius) — half height is the cylinder portion only
    float halfCylinder = std::max((h - 2.0f * r) * 0.5f, 0.0f);
    JPH::Shape *shape = new JPH::CapsuleShape(halfCylinder, r);
    JPH::Quat rotation = JPH::Quat::sIdentity();

    // Jolt capsule is always along Y. Rotate if direction != Y.
    if (m_direction == 0) {
        // Rotate capsule from Y to X
        rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisZ(), JPH::JPH_PI * 0.5f);
    } else if (m_direction == 2) {
        // Rotate capsule from Y to Z
        rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), JPH::JPH_PI * 0.5f);
    }

    glm::vec3 center = GetCenter();
    return new JPH::RotatedTranslatedShape(JPH::Vec3(center.x, center.y, center.z), rotation, shape);
}

// ============================================================================
// Serialization
// ============================================================================

std::string CapsuleCollider::Serialize() const
{
    auto baseJson = nlohmann::json::parse(Collider::Serialize());
    baseJson["radius"] = m_radius;
    baseJson["height"] = m_height;
    baseJson["direction"] = m_direction;
    return baseJson.dump();
}

bool CapsuleCollider::Deserialize(const std::string &jsonStr)
{
    if (!Collider::Deserialize(jsonStr))
        return false;

    try {
        auto j = nlohmann::json::parse(jsonStr);
        if (j.contains("radius"))
            m_radius = j["radius"].get<float>();
        if (j.contains("height"))
            m_height = j["height"].get<float>();
        if (j.contains("direction"))
            m_direction = j["direction"].get<int>();
        RebuildShape();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace infengine
