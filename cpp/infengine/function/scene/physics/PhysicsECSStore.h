#pragma once

/**
 * @file PhysicsECSStore.h
 * @brief Contiguous memory pools for Collider and Rigidbody hot data.
 *
 * Mirrors the TransformECSStore pattern: Component classes hold a Handle into
 * the pool; every-frame sync loops iterate contiguous data for cache-friendly
 * access.
 */

#include "core/types/InfContiguousPool.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace infengine
{

class Collider;
class Rigidbody;

// ============================================================================
// Pooled Collider data (hot path — touched every SyncCollidersToPhysics)
// ============================================================================

struct ColliderECSData
{
    // ---- Identity ----
    Collider *owner = nullptr;

    // ---- Jolt body ----
    uint32_t bodyId = 0xFFFFFFFF;
    bool bodyInBroadphase = false;

    // ---- Properties ----
    bool isTrigger = false;
    glm::vec3 center{0.0f};
    float friction = 0.4f;   ///< Dynamic friction [0..1] (Jolt default 0.2, Unity default 0.4)
    float bounciness = 0.0f; ///< Restitution / bounciness [0..1]

    // ---- Cached sync state (avoids per-frame Jolt reads) ----
    glm::vec3 lastSyncedPos{0.0f};
    glm::quat lastSyncedRot{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 lastScale{1.0f};

    // ---- Misc ----
    bool deserialized = false;
    Rigidbody *cachedRigidbody = nullptr;
};

// ============================================================================
// Pooled Rigidbody data (hot path — touched every SyncPhysicsToTransform)
// ============================================================================

struct RigidbodyECSData
{
    // ---- Identity ----
    Rigidbody *owner = nullptr;

    // ---- Serialized properties ----
    float mass = 1.0f;
    float drag = 0.0f;
    float angularDrag = 0.05f;
    bool useGravity = true;
    bool isKinematic = false;
    int constraints = 0;
    int collisionDetectionMode = 0;
    int interpolation = 0;
    float maxAngularVelocity = 7.0f;
    float maxLinearVelocity = 500.0f;
    float maxDepenetrationVelocity = 1e10f;

    // ---- Sync cache (external-move detection) ----
    glm::vec3 lastSyncedPosition{0.0f};
    glm::quat lastSyncedRotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool hasSyncedOnce = false;

    // ---- Physics interpolation cache ----
    glm::vec3 previousPhysicsPosition{0.0f};
    glm::quat previousPhysicsRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 currentPhysicsPosition{0.0f};
    glm::quat currentPhysicsRotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool hasPhysicsPose = false;
};

// ============================================================================
// PhysicsECSStore — singleton managing both pools
// ============================================================================

class PhysicsECSStore
{
  public:
    using ColliderHandle = InfContiguousPool<ColliderECSData>::Handle;
    using RigidbodyHandle = InfContiguousPool<RigidbodyECSData>::Handle;

    static PhysicsECSStore &Instance();

    // ---- Collider pool ----
    ColliderHandle AllocateCollider(Collider *owner);
    void ReleaseCollider(ColliderHandle handle);
    [[nodiscard]] bool IsValid(ColliderHandle handle) const;
    ColliderECSData &GetCollider(ColliderHandle handle);
    const ColliderECSData &GetCollider(ColliderHandle handle) const;
    [[nodiscard]] std::vector<ColliderHandle> GetAliveColliderHandles() const
    {
        return m_colliderPool.GetAliveHandles();
    }

    // ---- Rigidbody pool ----
    RigidbodyHandle AllocateRigidbody(Rigidbody *owner);
    void ReleaseRigidbody(RigidbodyHandle handle);
    [[nodiscard]] bool IsValid(RigidbodyHandle handle) const;
    RigidbodyECSData &GetRigidbody(RigidbodyHandle handle);
    const RigidbodyECSData &GetRigidbody(RigidbodyHandle handle) const;
    [[nodiscard]] std::vector<RigidbodyHandle> GetAliveRigidbodyHandles() const
    {
        return m_rigidbodyPool.GetAliveHandles();
    }

  private:
    PhysicsECSStore() = default;

    InfContiguousPool<ColliderECSData> m_colliderPool;
    InfContiguousPool<RigidbodyECSData> m_rigidbodyPool;
};

} // namespace infengine
