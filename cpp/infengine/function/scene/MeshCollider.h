/**
 * @file MeshCollider.h
 * @brief Triangle mesh / convex hull collider (Unity: MeshCollider).
 */

#pragma once

#include "Collider.h"

namespace infengine
{

class MeshCollider : public Collider
{
  public:
    MeshCollider() = default;
    ~MeshCollider() override = default;

    [[nodiscard]] const char *GetTypeName() const override
    {
        return "MeshCollider";
    }

    [[nodiscard]] bool IsConvex() const
    {
        return m_convex;
    }
    void SetConvex(bool convex);

    [[nodiscard]] void *CreateJoltShapeRaw() const override;

    [[nodiscard]] std::string Serialize() const override;
    bool Deserialize(const std::string &jsonStr) override;

    void AutoFitToMesh() override;

  private:
    bool CollectMeshGeometry(std::vector<glm::vec3> &outVertices, std::vector<uint32_t> &outIndices) const;

    bool m_convex = false;
};

} // namespace infengine
