#include "PhysicsContactListener.h"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <glm/glm.hpp>

namespace infengine
{

void InfContactListener::PreStep()
{
    m_rawEvents.clear();
    m_events.clear();
    for (auto &[key, state] : m_contactPairs)
        state.touchedThisStep = false;
}

void InfContactListener::ClearAll()
{
    m_rawEvents.clear();
    m_events.clear();
    m_contactPairs.clear();
}

void InfContactListener::InvalidatePairsForBody(uint32_t bodyId)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_contactPairs.begin();
    while (it != m_contactPairs.end()) {
        uint32_t idA = static_cast<uint32_t>(it->first >> 32);
        uint32_t idB = static_cast<uint32_t>(it->first & 0xFFFFFFFF);
        if (idA == bodyId || idB == bodyId)
            it = m_contactPairs.erase(it);
        else
            ++it;
    }
}

void InfContactListener::PushEvent(ContactEventType type, const JPH::Body &bodyA, const JPH::Body &bodyB,
                                   const JPH::ContactManifold *manifold)
{
    ContactEvent evt;
    evt.type = type;
    evt.bodyIdA = bodyA.GetID().GetIndexAndSequenceNumber();
    evt.bodyIdB = bodyB.GetID().GetIndexAndSequenceNumber();

    if (manifold) {
        evt.subShapeIdA = manifold->mSubShapeID1.GetValue();
        evt.subShapeIdB = manifold->mSubShapeID2.GetValue();
        // Use first contact point if available
        if (manifold->mRelativeContactPointsOn1.size() > 0) {
            JPH::Vec3 wp = manifold->GetWorldSpaceContactPointOn1(0);
            evt.contactPoint = glm::vec3(wp.GetX(), wp.GetY(), wp.GetZ());
        }
        JPH::Vec3 n = manifold->mWorldSpaceNormal;
        evt.contactNormal = glm::vec3(n.GetX(), n.GetY(), n.GetZ());
    }

    // Relative velocity
    JPH::Vec3 velA = bodyA.GetLinearVelocity();
    JPH::Vec3 velB = bodyB.GetLinearVelocity();
    JPH::Vec3 relVel = velA - velB;
    evt.relativeVelocity = glm::vec3(relVel.GetX(), relVel.GetY(), relVel.GetZ());

    std::lock_guard<std::mutex> lock(m_mutex);
    m_rawEvents.push_back(evt);
}

void InfContactListener::OnContactAdded(const JPH::Body &inBody1, const JPH::Body &inBody2,
                                        const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings)
{
    bool isSensor = inBody1.IsSensor() || inBody2.IsSensor();
    ContactEventType type = isSensor ? ContactEventType::TriggerEnter : ContactEventType::CollisionEnter;
    PushEvent(type, inBody1, inBody2, &inManifold);
}

void InfContactListener::OnContactPersisted(const JPH::Body &inBody1, const JPH::Body &inBody2,
                                            const JPH::ContactManifold &inManifold, JPH::ContactSettings &ioSettings)
{
    bool isSensor = inBody1.IsSensor() || inBody2.IsSensor();
    ContactEventType type = isSensor ? ContactEventType::TriggerStay : ContactEventType::CollisionStay;
    PushEvent(type, inBody1, inBody2, &inManifold);
}

void InfContactListener::OnContactRemoved(const JPH::SubShapeIDPair &inSubShapePair)
{
    // On removal we don't have full Body references — extract IDs from the pair.
    ContactEvent evt;
    // SubShapeIDPair stores BodyIDs for both bodies.
    evt.bodyIdA = inSubShapePair.GetBody1ID().GetIndexAndSequenceNumber();
    evt.bodyIdB = inSubShapePair.GetBody2ID().GetIndexAndSequenceNumber();
    evt.subShapeIdA = inSubShapePair.GetSubShapeID1().GetValue();
    evt.subShapeIdB = inSubShapePair.GetSubShapeID2().GetValue();
    // We cannot tell if it was a sensor pair from the SubShapeIDPair alone.
    // Store as CollisionExit; DispatchContactEvents will check isTrigger on
    // the Collider* to re-classify.
    evt.type = ContactEventType::CollisionExit;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_rawEvents.push_back(evt);
}

// ============================================================================
// ResolveEvents — post-step pair tracking (suppresses sleep-related spurious
// Enter/Exit to match Unity OnCollision semantics)
// ============================================================================

void InfContactListener::ResolveEvents(JPH::BodyInterface &bodyInterface)
{
    for (const auto &raw : m_rawEvents) {
        uint64_t key = MakePairKey(raw.bodyIdA, raw.bodyIdB);

        switch (raw.type) {
        case ContactEventType::CollisionEnter:
        case ContactEventType::TriggerEnter: {
            auto it = m_contactPairs.find(key);
            if (it != m_contactPairs.end() && it->second.sleeping) {
                // Body woke up — pair already tracked, suppress duplicate Enter.
                it->second.sleeping = false;
                it->second.touchedThisStep = true;
            } else {
                // Genuine new contact.
                m_contactPairs[key] = {true, false};
                m_events.push_back(raw);
            }
            break;
        }

        case ContactEventType::CollisionStay:
        case ContactEventType::TriggerStay: {
            auto it = m_contactPairs.find(key);
            if (it != m_contactPairs.end())
                it->second.touchedThisStep = true;
            m_events.push_back(raw);
            break;
        }

        case ContactEventType::CollisionExit:
            // Note: OnContactRemoved always tags as CollisionExit because Body
            // refs aren't available. DispatchContactEvents re-classifies to
            // TriggerExit when either Collider is a trigger.
            {
                JPH::BodyID joltA(raw.bodyIdA);
                JPH::BodyID joltB(raw.bodyIdB);

                // A "sleeping dynamic" body is still in the broadphase but not
                // active, and is NOT a static body (statics are always inactive).
                bool aAdded = bodyInterface.IsAdded(joltA);
                bool bAdded = bodyInterface.IsAdded(joltB);

                bool aSleeping = aAdded && !bodyInterface.IsActive(joltA) &&
                                 bodyInterface.GetMotionType(joltA) != JPH::EMotionType::Static;
                bool bSleeping = bAdded && !bodyInterface.IsActive(joltB) &&
                                 bodyInterface.GetMotionType(joltB) != JPH::EMotionType::Static;

                if (aSleeping || bSleeping) {
                    // Sleep-related removal — suppress Exit, mark pair as sleeping.
                    auto it = m_contactPairs.find(key);
                    if (it != m_contactPairs.end())
                        it->second.sleeping = true;
                } else {
                    // Real separation or body removed from broadphase.
                    m_contactPairs.erase(key);
                    m_events.push_back(raw);
                }
                break;
            }

        default:
            m_events.push_back(raw);
            break;
        }
    }

    // ========================================================================
    // Sweep sleeping pairs: if the previously-sleeping body is now active but
    // we received no Added/Persisted this step, the bodies separated after
    // waking. Emit deferred Exit for these pairs.
    // ========================================================================
    std::vector<uint64_t> expiredPairs;
    for (auto &[key, state] : m_contactPairs) {
        if (!state.sleeping || state.touchedThisStep)
            continue;

        uint32_t idA = static_cast<uint32_t>(key >> 32);
        uint32_t idB = static_cast<uint32_t>(key & 0xFFFFFFFF);

        JPH::BodyID joltA(idA);
        JPH::BodyID joltB(idB);

        // Check if a previously-sleeping dynamic body woke up or was removed.
        bool aWokeUp = bodyInterface.IsAdded(joltA) && bodyInterface.IsActive(joltA);
        bool bWokeUp = bodyInterface.IsAdded(joltB) && bodyInterface.IsActive(joltB);
        bool aRemoved = !bodyInterface.IsAdded(joltA);
        bool bRemoved = !bodyInterface.IsAdded(joltB);

        if (aWokeUp || bWokeUp || aRemoved || bRemoved) {
            ContactEvent exitEvt{};
            exitEvt.type = ContactEventType::CollisionExit;
            exitEvt.bodyIdA = idA;
            exitEvt.bodyIdB = idB;
            m_events.push_back(exitEvt);
            expiredPairs.push_back(key);
        }
    }

    for (uint64_t key : expiredPairs)
        m_contactPairs.erase(key);
}

} // namespace infengine
