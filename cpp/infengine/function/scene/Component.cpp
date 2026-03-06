#include "Component.h"
#include "GameObject.h"
#include "Scene.h"
#include "Transform.h"
#include <InfLog.h>
#include <atomic>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace infengine
{

// Static component ID generator
static std::atomic<uint64_t> s_nextComponentID{1};

uint64_t Component::GenerateComponentID()
{
    return s_nextComponentID.fetch_add(1, std::memory_order_relaxed);
}

void Component::EnsureNextComponentID(uint64_t id)
{
    uint64_t next = id + 1;
    uint64_t current = s_nextComponentID.load(std::memory_order_relaxed);
    while (current < next && !s_nextComponentID.compare_exchange_weak(current, next, std::memory_order_relaxed)) {
        // retry with updated current
    }
}

Component::Component() : m_componentId(GenerateComponentID()), m_wasEnabled(false)
{
}

void Component::CallAwake()
{
    if (m_hasAwake || m_hasDestroyed) {
        return;
    }
    m_hasAwake = true;
    Awake();
    // After awake, if enabled AND active in hierarchy, call OnEnable.
    if (m_enabled && m_gameObject && m_gameObject->IsActiveInHierarchy()) {
        CallOnEnable();
    }
}

void Component::CallStart()
{
    if (m_hasStarted || m_hasDestroyed) {
        return;
    }
    m_hasStarted = true;
    Start();
}

void Component::CallOnEnable()
{
    if (m_wasEnabled || m_hasDestroyed) {
        return;
    }
    m_wasEnabled = true;
    OnEnable();
}

void Component::CallOnDisable()
{
    if (!m_wasEnabled || m_hasDestroyed) {
        return;
    }
    m_wasEnabled = false;
    OnDisable();
}

void Component::CallOnDestroy()
{
    if (m_hasDestroyed) {
        return;
    }

    if (m_wasEnabled) {
        CallOnDisable();
    }

    m_enabled = false;
    m_hasDestroyed = true;
    OnDestroy();
}

void Component::CallOnValidate()
{
    OnValidate();
}

void Component::CallReset()
{
    Reset();
}

void Component::SetEnabled(bool enabled)
{
    if (m_hasDestroyed) {
        return;
    }

    if (m_enabled == enabled) {
        return;
    }

    m_enabled = enabled;

    // Unity semantics:
    // - OnEnable fires only when component is enabled and active in hierarchy.
    // - OnDisable fires when transitioning out of that effective-active state.
    // - Start fires once, first time component becomes effectively active in play mode.
    if (!m_hasAwake || !m_gameObject) {
        return;
    }

    Scene *scene = m_gameObject->GetScene();
    if (!scene) {
        return;
    }

    const bool lifecycleAllowed = scene->IsPlaying() || WantsEditModeLifecycle();
    if (!lifecycleAllowed) {
        return;
    }

    const bool effectiveActive = m_enabled && m_gameObject->IsActiveInHierarchy();
    if (effectiveActive) {
        CallOnEnable();
        if (scene->IsPlaying() && scene->HasStarted()) {
            scene->QueueComponentStart(this);
        }
    } else {
        CallOnDisable();
    }
}

void Component::SetComponentID(uint64_t id)
{
    m_componentId = id;
    EnsureNextComponentID(id);
}

Transform *Component::GetTransform() const
{
    if (m_gameObject) {
        return m_gameObject->GetTransform();
    }
    return nullptr;
}

std::string Component::Serialize() const
{
    json j;
    j["schema_version"] = 1;
    j["type"] = GetTypeName();
    j["enabled"] = m_enabled;
    j["execution_order"] = m_executionOrder;
    j["component_id"] = m_componentId;
    return j.dump(2);
}

bool Component::Deserialize(const std::string &jsonStr)
{
    try {
        json j = json::parse(jsonStr);
        if (!j.contains("schema_version")) {
            INFLOG_WARN("Component::Deserialize: missing 'schema_version' field (legacy data)");
        }
        if (j.contains("enabled")) {
            m_enabled = j["enabled"].get<bool>();
        }
        if (j.contains("execution_order")) {
            m_executionOrder = j["execution_order"].get<int>();
        }
        if (j.contains("component_id")) {
            m_componentId = j["component_id"].get<uint64_t>();
            EnsureNextComponentID(m_componentId);
        }
        return true;
    } catch (const std::exception &e) {
        return false;
    }
}

} // namespace infengine
