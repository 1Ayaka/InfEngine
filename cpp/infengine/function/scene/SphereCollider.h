/**
 * @file SphereCollider.h
 * @brief Sphere collider component (Unity: SphereCollider).
 */

#pragma once

#include "Collider.h"

namespace infengine
{

class SphereCollider : public Collider
{
  public:
    SphereCollider() = default;
    ~SphereCollider() override = default;

    [[nodiscard]] const char *GetTypeName() const override
    {
        return "SphereCollider";
    }

    // ====================================================================
    // Properties
    // ====================================================================

    [[nodiscard]] float GetRadius() const
    {
        return m_radius;
    }
    void SetRadius(float radius);

    // ====================================================================
    // Jolt shape
    // ====================================================================

    [[nodiscard]] void *CreateJoltShapeRaw() const override;

    // ====================================================================
    // Serialization
    // ====================================================================

    [[nodiscard]] std::string Serialize() const override;
    bool Deserialize(const std::string &jsonStr) override;

    void AutoFitToMesh() override;

  private:
    float m_radius = 0.5f;
};

} // namespace infengine
