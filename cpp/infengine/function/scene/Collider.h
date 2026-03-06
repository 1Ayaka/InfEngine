/**
 * @file Collider.h
 * @brief Base class for all physics collider components.
 *
 * Mirrors Unity's Collider API. Derived classes (BoxCollider, SphereCollider,
 * CapsuleCollider) override CreateJoltShape() to provide their specific geometry.
 *
 * A Collider without a Rigidbody sibling acts as a static collider (for raycasts,
 * overlap tests, etc.). With a Rigidbody it becomes dynamic.
 */

#pragma once

#include "Component.h"
#include "physics/PhysicsECSStore.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>

namespace infengine
{

// Forward declaration — Collider caches a Rigidbody* pointer but does not
// dereference it in the header (only in Collider.cpp which includes Rigidbody.h).
class Rigidbody;

/**
 * @brief Abstract base class for Collider components.
 *
 * NOTE: Jolt types are NOT exposed in this header to avoid Jolt include-order
 * issues. The virtual CreateJoltShapeRaw() returns void* which is actually
 * a JPH::Shape* with one reference added. Callers in .cpp files cast it.
 */
class Collider : public Component
{
  public:
    using ECSHandle = PhysicsECSStore::ColliderHandle;

    Collider();
    ~Collider() override;

    // ====================================================================
    // Lifecycle
    // ====================================================================

    void Awake() override;
    void OnEnable() override;
    void OnDisable() override;
    void OnDestroy() override;

    // ====================================================================
    // Properties (Unity-style)
    // ====================================================================

    /// @brief Is this collider a trigger? (Unity: Collider.isTrigger)
    [[nodiscard]] bool IsTrigger() const
    {
        return Data().isTrigger;
    }
    void SetIsTrigger(bool trigger);

    /// @brief Center offset in local space (Unity: Collider.center for Box/Capsule)
    [[nodiscard]] glm::vec3 GetCenter() const
    {
        return Data().center;
    }
    void SetCenter(const glm::vec3 &center);

    /// @brief Dynamic friction coefficient [0..1].
    [[nodiscard]] float GetFriction() const
    {
        return Data().friction;
    }
    void SetFriction(float friction);

    /// @brief Bounciness / restitution [0..1].
    [[nodiscard]] float GetBounciness() const
    {
        return Data().bounciness;
    }
    void SetBounciness(float bounciness);

    // ====================================================================
    // Jolt integration (opaque — Jolt types hidden from header)
    // ====================================================================

    /// @brief Create the Jolt collision shape. Returns a new-ed JPH::Shape* (caller
    ///        must wrap it in RefConst). Override in derived colliders.
    [[nodiscard]] virtual void *CreateJoltShapeRaw() const = 0;

    /// @brief Get the absolute world scale of the owning GameObject.
    ///        Returns (1,1,1) if no GameObject/Transform is available.
    [[nodiscard]] glm::vec3 GetWorldScale() const;

    /// @brief Get the Jolt body ID (0xFFFFFFFF = not registered).
    [[nodiscard]] uint32_t GetBodyId() const
    {
        return Data().bodyId;
    }

    /// @brief Sync the body transform with the GameObject's Transform.
    void SyncTransformToPhysics();

    /// Register body in PhysicsWorld (creates the Jolt body, does NOT add to broadphase).
    void RegisterBody();

    /// Unregister body from PhysicsWorld (removes from broadphase + destroys).
    void UnregisterBody();

    /// Add body to the Jolt broadphase (makes it visible to raycasts/queries).
    void AddToBroadphase();

    /// Remove body from the Jolt broadphase (invisible to raycasts, body kept alive).
    void RemoveFromBroadphase();

    // ====================================================================
    // Type info
    // ====================================================================

    [[nodiscard]] const char *GetTypeName() const override
    {
        return "Collider";
    }

    /// All Collider-derived types also match the base name "Collider",
    /// so that RequireComponent("Collider") is satisfied by BoxCollider etc.
    [[nodiscard]] bool IsComponentType(const std::string &typeName) const override
    {
        if (typeName == "Collider")
            return true;
        return std::string(GetTypeName()) == typeName;
    }

    // ====================================================================
    // Serialization
    // ====================================================================

    [[nodiscard]] std::string Serialize() const override;
    bool Deserialize(const std::string &jsonStr) override;

    /// @brief Auto-fit collider shape to sibling MeshRenderer bounds.
    ///        Called in Awake() for freshly-added colliders (not deserialized).
    ///        Override in derived classes to set size/radius/height from mesh AABB.
    virtual void AutoFitToMesh();

    /// @brief Cache (or invalidate) the sibling Rigidbody pointer.
    ///        Called by Rigidbody::OnEnable / OnDisable.
    void SetCachedRigidbody(Rigidbody *rb)
    {
        DataMut().cachedRigidbody = rb;
    }

    /// @brief Get the cached Rigidbody (may be nullptr).
    [[nodiscard]] Rigidbody *GetCachedRigidbody() const
    {
        return Data().cachedRigidbody;
    }

    /// @brief Update the cached last-synced position/rotation after physics step
    ///        writes back to Transform. Called by Rigidbody::SyncPhysicsToTransform.
    void SetLastSyncedTransform(const glm::vec3 &pos, const glm::quat &rot)
    {
        auto &d = DataMut();
        d.lastSyncedPos = pos;
        d.lastSyncedRot = rot;
    }

    /// @brief Get the ECS pool handle.
    [[nodiscard]] ECSHandle GetECSHandle() const
    {
        return m_ecsHandle;
    }

  protected:
    /// Called after shape parameters change to update the Jolt body.
    void RebuildShape();

    /// Pool-backed data — read access
    [[nodiscard]] const ColliderECSData &Data() const
    {
        return PhysicsECSStore::Instance().GetCollider(m_ecsHandle);
    }
    /// Pool-backed data — write access
    [[nodiscard]] ColliderECSData &DataMut() const
    {
        return PhysicsECSStore::Instance().GetCollider(m_ecsHandle);
    }

    ECSHandle m_ecsHandle;
};

} // namespace infengine
