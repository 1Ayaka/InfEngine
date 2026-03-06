/**
 * @file PhysicsECSStore.cpp
 * @brief Contiguous memory pools for Collider and Rigidbody data.
 */

#include "PhysicsECSStore.h"

namespace infengine
{

PhysicsECSStore &PhysicsECSStore::Instance()
{
    static PhysicsECSStore instance;
    return instance;
}

// ============================================================================
// Collider pool
// ============================================================================

PhysicsECSStore::ColliderHandle PhysicsECSStore::AllocateCollider(Collider *owner)
{
    ColliderHandle handle = m_colliderPool.Allocate();
    ColliderECSData &data = m_colliderPool.Get(handle);
    data = ColliderECSData{};
    data.owner = owner;
    return handle;
}

void PhysicsECSStore::ReleaseCollider(ColliderHandle handle)
{
    if (!m_colliderPool.IsAlive(handle))
        return;
    m_colliderPool.Get(handle).owner = nullptr;
    m_colliderPool.Free(handle);
}

bool PhysicsECSStore::IsValid(ColliderHandle handle) const
{
    return m_colliderPool.IsAlive(handle);
}

ColliderECSData &PhysicsECSStore::GetCollider(ColliderHandle handle)
{
    return m_colliderPool.Get(handle);
}

const ColliderECSData &PhysicsECSStore::GetCollider(ColliderHandle handle) const
{
    return m_colliderPool.Get(handle);
}

// ============================================================================
// Rigidbody pool
// ============================================================================

PhysicsECSStore::RigidbodyHandle PhysicsECSStore::AllocateRigidbody(Rigidbody *owner)
{
    RigidbodyHandle handle = m_rigidbodyPool.Allocate();
    RigidbodyECSData &data = m_rigidbodyPool.Get(handle);
    data = RigidbodyECSData{};
    data.owner = owner;
    return handle;
}

void PhysicsECSStore::ReleaseRigidbody(RigidbodyHandle handle)
{
    if (!m_rigidbodyPool.IsAlive(handle))
        return;
    m_rigidbodyPool.Get(handle).owner = nullptr;
    m_rigidbodyPool.Free(handle);
}

bool PhysicsECSStore::IsValid(RigidbodyHandle handle) const
{
    return m_rigidbodyPool.IsAlive(handle);
}

RigidbodyECSData &PhysicsECSStore::GetRigidbody(RigidbodyHandle handle)
{
    return m_rigidbodyPool.Get(handle);
}

const RigidbodyECSData &PhysicsECSStore::GetRigidbody(RigidbodyHandle handle) const
{
    return m_rigidbodyPool.Get(handle);
}

} // namespace infengine
