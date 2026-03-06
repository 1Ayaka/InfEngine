#pragma once

/**
 * @file PhysicsContactListener.h
 * @brief Jolt ContactListener implementation that collects collision/trigger
 *        events for dispatch to Unity-style component callbacks.
 *
 * Events are buffered during PhysicsWorld::Step() and flushed by
 * PhysicsWorld::DispatchContactEvents() immediately after.
 */

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/ContactListener.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

// Forward-declare Jolt BodyInterface for ResolveEvents parameter
namespace JPH
{
class BodyInterface;
}

namespace infengine
{

class Collider;
class GameObject;

// ============================================================================
// Collision — Unity-style collision data passed to callbacks
// ============================================================================

/**
 * @brief Data about a collision event (Unity: Collision).
 *
 * Passed to OnCollisionEnter / OnCollisionStay / OnCollisionExit.
 * For trigger events, only collider/gameObject are meaningful.
 */
struct CollisionInfo
{
    Collider *collider = nullptr;     ///< The other collider involved
    GameObject *gameObject = nullptr; ///< The other GameObject
    glm::vec3 contactPoint{0.0f};     ///< World-space contact point (first contact)
    glm::vec3 contactNormal{0.0f};    ///< Contact normal (points from other → this)
    glm::vec3 relativeVelocity{0.0f}; ///< Relative velocity between the bodies
    float impulse = 0.0f;             ///< Total impulse magnitude of the contact
};

// ============================================================================
// Internal event struct — used between listener and dispatch
// ============================================================================

enum class ContactEventType : uint8_t
{
    CollisionEnter,
    CollisionStay,
    CollisionExit,
    TriggerEnter,
    TriggerStay,
    TriggerExit,
};

struct ContactEvent
{
    ContactEventType type;
    uint32_t bodyIdA; ///< Jolt body index+sequence for body A
    uint32_t bodyIdB; ///< Jolt body index+sequence for body B
    uint32_t subShapeIdA = 0;
    uint32_t subShapeIdB = 0;
    glm::vec3 contactPoint{0.0f};
    glm::vec3 contactNormal{0.0f};
    glm::vec3 relativeVelocity{0.0f};
    float impulse = 0.0f;
};

// ============================================================================
// InfContactListener — Jolt callback implementation
// ============================================================================

class InfContactListener : public JPH::ContactListener
{
  public:
    /// Reset per-step buffers. Call before PhysicsWorld::Step().
    void PreStep();

    /// Process raw contact events through pair tracking to produce final
    /// events that suppress sleep-related spurious Enter/Exit.
    /// Call after Step(), before DispatchContactEvents().
    void ResolveEvents(JPH::BodyInterface &bodyInterface);

    /// Full reset — clears all cached pair state (call on scene change / shutdown).
    void ClearAll();

    /// Remove all tracked contact pairs that involve @p bodyId.
    /// Call when a body's sensor flag changes at runtime so that the next
    /// step produces fresh Enter events instead of suppressing them as
    /// wake-from-sleep duplicates.
    void InvalidatePairsForBody(uint32_t bodyId);

    /// Get resolved events for dispatch.
    const std::vector<ContactEvent> &GetEvents() const
    {
        return m_events;
    }

    // -- JPH::ContactListener overrides --

    void OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold,
                        JPH::ContactSettings &ioSettings) override;

    void OnContactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2, const JPH::ContactManifold &inManifold,
                            JPH::ContactSettings &ioSettings) override;

    void OnContactRemoved(const JPH::SubShapeIDPair &inSubShapePair) override;

  private:
    void PushEvent(ContactEventType type, const JPH::Body &bodyA, const JPH::Body &bodyB,
                   const JPH::ContactManifold *manifold);

    /// Create a symmetric key for a body-pair (order-independent).
    static uint64_t MakePairKey(uint32_t a, uint32_t b)
    {
        if (a > b)
            std::swap(a, b);
        return (static_cast<uint64_t>(a) << 32) | b;
    }

    std::vector<ContactEvent> m_rawEvents; ///< Buffered from Jolt callbacks (during Step)
    std::vector<ContactEvent> m_events;    ///< Resolved events for dispatch

    /// Persistent pair tracking across physics steps.
    struct PairState
    {
        bool touchedThisStep = false;
        bool sleeping = false; ///< Exit was suppressed because a body went to sleep
    };
    std::unordered_map<uint64_t, PairState> m_contactPairs;

    std::mutex m_mutex; ///< Jolt may call from worker threads
};

} // namespace infengine
