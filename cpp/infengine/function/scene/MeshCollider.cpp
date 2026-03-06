/**
 * @file MeshCollider.cpp
 * @brief MeshCollider implementation — triangle mesh or convex hull shape creation.
 */

// Jolt/Jolt.h MUST be the very first include in this TU
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>

#include "MeshCollider.h"

#include "ComponentFactory.h"
#include "GameObject.h"
#include "MeshRenderer.h"
#include "Rigidbody.h"

#include <nlohmann/json.hpp>

namespace infengine
{

INFENGINE_REGISTER_COMPONENT("MeshCollider", MeshCollider)

void MeshCollider::SetConvex(bool convex)
{
    if (m_convex == convex) {
        return;
    }
    m_convex = convex;
    RebuildShape();
}

void MeshCollider::AutoFitToMesh()
{
    auto *go = GetGameObject();
    if (!go) {
        return;
    }

    auto *mr = go->GetComponent<MeshRenderer>();
    if (!mr) {
        return;
    }

    DataMut().center = (mr->GetLocalBoundsMin() + mr->GetLocalBoundsMax()) * 0.5f;
}

bool MeshCollider::CollectMeshGeometry(std::vector<glm::vec3> &outVertices, std::vector<uint32_t> &outIndices) const
{
    outVertices.clear();
    outIndices.clear();

    auto *go = GetGameObject();
    if (!go) {
        return false;
    }

    auto *mr = go->GetComponent<MeshRenderer>();
    glm::vec3 scale = GetWorldScale();

    if (mr && mr->HasInlineMesh() && !mr->GetInlineVertices().empty() && mr->GetInlineIndices().size() >= 3) {
        outVertices.reserve(mr->GetInlineVertices().size());
        for (const auto &vertex : mr->GetInlineVertices()) {
            outVertices.emplace_back(vertex.pos.x * scale.x, vertex.pos.y * scale.y, vertex.pos.z * scale.z);
        }
        outIndices = mr->GetInlineIndices();
        return true;
    }

    glm::vec3 boundsMin(-0.5f, -0.5f, -0.5f);
    glm::vec3 boundsMax(0.5f, 0.5f, 0.5f);
    if (mr) {
        boundsMin = mr->GetLocalBoundsMin();
        boundsMax = mr->GetLocalBoundsMax();
    }

    glm::vec3 minScaled(boundsMin.x * scale.x, boundsMin.y * scale.y, boundsMin.z * scale.z);
    glm::vec3 maxScaled(boundsMax.x * scale.x, boundsMax.y * scale.y, boundsMax.z * scale.z);

    outVertices = {
        {minScaled.x, minScaled.y, minScaled.z}, {maxScaled.x, minScaled.y, minScaled.z},
        {maxScaled.x, maxScaled.y, minScaled.z}, {minScaled.x, maxScaled.y, minScaled.z},
        {minScaled.x, minScaled.y, maxScaled.z}, {maxScaled.x, minScaled.y, maxScaled.z},
        {maxScaled.x, maxScaled.y, maxScaled.z}, {minScaled.x, maxScaled.y, maxScaled.z},
    };

    outIndices = {
        0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1, 3, 2, 6, 3, 6, 7, 1, 5, 6, 1, 6, 2, 0, 3, 7, 0, 7, 4,
    };
    return true;
}

void *MeshCollider::CreateJoltShapeRaw() const
{
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
    if (!CollectMeshGeometry(vertices, indices) || vertices.empty()) {
        JPH::Shape *fallback = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
        glm::vec3 center = GetCenter();
        if (center != glm::vec3(0.0f)) {
            fallback = new JPH::RotatedTranslatedShape(JPH::Vec3(center.x, center.y, center.z), JPH::Quat::sIdentity(),
                                                       fallback);
        }
        return fallback;
    }

    bool useConvex = m_convex;
    if (auto *rb = GetCachedRigidbody(); rb && !rb->IsKinematic()) {
        useConvex = true;
    }

    JPH::Shape *shape = nullptr;
    if (useConvex) {
        JPH::Array<JPH::Vec3> hullPoints;
        hullPoints.reserve(static_cast<int>(vertices.size()));
        for (const auto &v : vertices) {
            hullPoints.push_back(JPH::Vec3(v.x, v.y, v.z));
        }

        JPH::ConvexHullShapeSettings settings(hullPoints);
        JPH::ShapeSettings::ShapeResult result = settings.Create();
        if (result.HasError()) {
            shape = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
        } else {
            shape = const_cast<JPH::Shape *>(result.Get().GetPtr());
            shape->AddRef();
        }
    } else {
        JPH::MeshShapeSettings settings;
        settings.mTriangleVertices.reserve(static_cast<int>(vertices.size()));
        for (const auto &v : vertices) {
            settings.mTriangleVertices.emplace_back(v.x, v.y, v.z);
        }
        settings.mIndexedTriangles.reserve(static_cast<int>(indices.size() / 3));
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            settings.mIndexedTriangles.emplace_back(indices[i], indices[i + 1], indices[i + 2]);
        }
        settings.SetEmbedded();

        JPH::ShapeSettings::ShapeResult result = settings.Create();
        if (result.HasError()) {
            shape = new JPH::BoxShape(JPH::Vec3(0.5f, 0.5f, 0.5f));
        } else {
            shape = const_cast<JPH::Shape *>(result.Get().GetPtr());
            shape->AddRef();
        }
    }

    glm::vec3 center = GetCenter();
    if (center != glm::vec3(0.0f)) {
        shape = new JPH::RotatedTranslatedShape(JPH::Vec3(center.x, center.y, center.z), JPH::Quat::sIdentity(), shape);
    }

    return shape;
}

std::string MeshCollider::Serialize() const
{
    auto baseJson = nlohmann::json::parse(Collider::Serialize());
    baseJson["convex"] = m_convex;
    return baseJson.dump();
}

bool MeshCollider::Deserialize(const std::string &jsonStr)
{
    if (!Collider::Deserialize(jsonStr)) {
        return false;
    }

    try {
        auto j = nlohmann::json::parse(jsonStr);
        m_convex = j.value("convex", false);
        RebuildShape();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace infengine
