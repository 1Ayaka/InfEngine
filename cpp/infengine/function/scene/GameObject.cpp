#include "GameObject.h"
#include "Collider.h"
#include "ComponentFactory.h"
#include "MeshRenderer.h"
#include "PyComponentProxy.h"
#include "Scene.h"
#include "physics/PhysicsWorld.h"
#include <InfLog.h>
#include <algorithm>
#include <atomic>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace py = pybind11;

using json = nlohmann::json;

namespace infengine
{

// Static ID generator
static std::atomic<uint64_t> s_nextID{1};

uint64_t GameObject::GenerateID()
{
    return s_nextID.fetch_add(1, std::memory_order_relaxed);
}

void GameObject::EnsureNextID(uint64_t id)
{
    uint64_t next = id + 1;
    uint64_t current = s_nextID.load(std::memory_order_relaxed);
    while (current < next && !s_nextID.compare_exchange_weak(current, next, std::memory_order_relaxed)) {
        // retry with updated current
    }
}

GameObject::GameObject(const std::string &name) : m_name(name), m_id(GenerateID())
{
    // Transform is automatically part of GameObject
    m_transform.SetGameObject(this);
}

void GameObject::SetLayer(int layer)
{
    const int clamped = (layer >= 0 && layer < 32) ? layer : 0;
    if (m_layer == clamped) {
        return;
    }

    m_layer = clamped;

    auto &physics = PhysicsWorld::Instance();
    if (!physics.IsInitialized()) {
        return;
    }

    std::unordered_set<uint32_t> updatedBodies;
    auto colliders = GetComponents<Collider>();
    for (auto *collider : colliders) {
        if (!collider) {
            continue;
        }

        const uint32_t bodyId = collider->GetBodyId();
        if (bodyId == 0xFFFFFFFF || !updatedBodies.insert(bodyId).second) {
            continue;
        }

        physics.SetBodyGameLayer(bodyId, m_layer);
    }
}

void GameObject::SetScene(Scene *scene)
{
    m_scene = scene;
    m_transform.SetGameObject(this);
    for (const auto &child : m_children) {
        if (child) {
            child->SetScene(scene);
        }
    }
}

GameObject::~GameObject()
{
    // Unregister self from Scene lookup
    if (m_scene) {
        m_scene->UnregisterGameObject(m_id);
    }

    // Phase 1: Run lifecycle callbacks while ALL components are still alive.
    // This lets OnDisable/OnDestroy safely call GetComponents<>() on siblings.
    for (auto &comp : m_components) {
        comp->CallOnDestroy();
    }

    // Phase 2: Move components out of the vector BEFORE destructors run.
    // During vector::clear(), C++ destructors fire while the vector is
    // partially destroyed — calling GetComponents<>() from a destructor
    // would dynamic_cast on dangling pointers (undefined behaviour).
    // Moving into a local vector ensures m_components is empty so any
    // accidental GetComponents<>() call from a destructor returns [].
    auto dying = std::move(m_components);
    // m_components is now empty — safe for any destructor that reads it.
    dying.clear(); // Destroy unique_ptrs; destructors see empty m_components.
}

bool GameObject::IsActiveInHierarchy() const
{
    if (!m_active)
        return false;
    if (m_parent)
        return m_parent->IsActiveInHierarchy();
    return true;
}

void GameObject::HandleActiveStateChanged(bool wasActiveInHierarchy, bool isActiveInHierarchy)
{
    if (wasActiveInHierarchy == isActiveInHierarchy) {
        return;
    }

    // In edit mode only propagate lifecycle to components that explicitly
    // opt in (pure C++ components).  PyComponentProxy skips this so Python
    // awake/on_enable/on_disable are not called outside play mode.
    bool playing = m_scene && m_scene->IsPlaying();
    if (!m_scene) {
        return;
    }

    if (isActiveInHierarchy) {
        std::vector<Component *> components = GetComponentsInExecutionOrder();
        for (Component *comp : components) {
            if (!comp)
                continue;
            if (!playing && !comp->WantsEditModeLifecycle())
                continue;
            if (!comp->HasAwake()) {
                comp->CallAwake();
            }
            if (comp->IsEnabled()) {
                comp->CallOnEnable();
                if (playing && m_scene->HasStarted()) {
                    m_scene->QueueComponentStart(comp);
                }
            }
        }
    } else {
        std::vector<Component *> components = GetComponentsInExecutionOrder();
        for (Component *comp : components) {
            if (!comp)
                continue;
            if (!playing && !comp->WantsEditModeLifecycle())
                continue;
            if (comp->IsEnabled() && comp->HasAwake()) {
                comp->CallOnDisable();
            }
        }
    }

    for (size_t i = 0; i < m_children.size(); ++i) {
        auto &child = m_children[i];
        if (!child)
            continue;
        bool childWasActive = wasActiveInHierarchy && child->m_active;
        bool childIsActive = isActiveInHierarchy && child->m_active;
        child->HandleActiveStateChanged(childWasActive, childIsActive);
    }
}

std::vector<Component *> GameObject::GetComponentsInExecutionOrder() const
{
    std::vector<Component *> result;
    result.reserve(m_components.size());

    for (const auto &comp : m_components) {
        if (comp) {
            result.push_back(comp.get());
        }
    }

    std::stable_sort(result.begin(), result.end(), [](const Component *a, const Component *b) {
        if (a->GetExecutionOrder() != b->GetExecutionOrder()) {
            return a->GetExecutionOrder() < b->GetExecutionOrder();
        }
        return a->GetComponentID() < b->GetComponentID();
    });

    return result;
}

void GameObject::SetActive(bool active)
{
    bool wasActiveInHierarchy = IsActiveInHierarchy();

    if (m_active == active) {
        return;
    }

    m_active = active;

    bool isActiveInHierarchy = IsActiveInHierarchy();
    HandleActiveStateChanged(wasActiveInHierarchy, isActiveInHierarchy);
}

GameObject *GameObject::GetChild(size_t index) const
{
    if (index < m_children.size()) {
        return m_children[index].get();
    }
    return nullptr;
}

void GameObject::SetParent(GameObject *newParent, bool worldPositionStays)
{
    if (newParent == m_parent)
        return;

    bool wasActiveInHierarchy = IsActiveInHierarchy();

    // Prevent circular reference
    if (newParent) {
        GameObject *ancestor = newParent;
        while (ancestor) {
            if (ancestor == this)
                return; // Cannot be child of own descendant
            ancestor = ancestor->m_parent;
        }
    }

    // Cache world transform before reparenting
    glm::vec3 savedWorldPos;
    glm::quat savedWorldRot;
    glm::vec3 savedWorldScale;
    if (worldPositionStays) {
        savedWorldPos = m_transform.GetWorldPosition();
        savedWorldRot = m_transform.GetWorldRotation();
        savedWorldScale = m_transform.GetWorldScale();
    }

    std::unique_ptr<GameObject> selfPtr;

    // 1. Detach from current owner
    if (m_parent) {
        selfPtr = m_parent->DetachChild(this);
    } else if (m_scene) {
        selfPtr = m_scene->DetachRootObject(this);
    }

    if (!selfPtr) {
        // Should not happen unless object is in limbo state
        return;
    }

    // 2. Attach to new owner
    if (newParent) {
        m_parent = newParent;
        // Ensure scene matches new parent
        if (newParent->m_scene != m_scene) {
            m_scene = newParent->m_scene;
        }
        newParent->AttachChild(std::move(selfPtr));
    } else {
        m_parent = nullptr;
        // Attached to root
        if (m_scene) {
            m_scene->AttachRootObject(std::move(selfPtr));
        }
    }

    // 3. Restore world transform after hierarchy change
    if (worldPositionStays) {
        m_transform.SetWorldPosition(savedWorldPos);
        m_transform.SetWorldRotation(savedWorldRot);
        m_transform.SetWorldScale(savedWorldScale);
    } else {
        // Parent changed without adjusting local values — world matrix is now stale
        m_transform.InvalidateWorldMatrix();
    }

    bool isActiveInHierarchy = IsActiveInHierarchy();
    HandleActiveStateChanged(wasActiveInHierarchy, isActiveInHierarchy);
}

Component *GameObject::AddExistingComponent(std::unique_ptr<Component> component)
{
    if (!component)
        return nullptr;

    Component *ptr = component.get();
    ptr->SetGameObject(this);
    m_components.push_back(std::move(component));
    PostAddComponent(ptr);
    return ptr;
}

Component *GameObject::AddComponentByTypeName(const std::string &typeName)
{
    if (typeName.empty() || typeName == "Transform") {
        return nullptr;
    }

    std::unique_ptr<Component> component = ComponentFactory::Create(typeName);
    if (!component) {
        return nullptr;
    }

    Component *ptr = component.get();
    ptr->SetGameObject(this);
    m_components.push_back(std::move(component));
    PostAddComponent(ptr);
    return ptr;
}

void GameObject::PostAddComponent(Component *component)
{
    if (!component || !m_scene) {
        return;
    }

    // Always awaken components that want edit-mode lifecycle (all pure C++
    // components — MeshRenderer, Collider, Rigidbody, …).  This ensures their
    // OnEnable fires and they register themselves with SceneManager registries
    // immediately, even when the scene is not in play mode.
    // PyComponentProxy overrides WantsEditModeLifecycle() to return false so
    // user Python code (awake / on_enable) is never called outside play mode.
    if (!m_scene->IsPlaying() && !component->WantsEditModeLifecycle()) {
        return;
    }

    component->CallAwake();

    // Start is always play-mode only.
    if (m_scene->IsPlaying() && m_scene->HasStarted() && component->IsEnabled() && IsActiveInHierarchy()) {
        m_scene->QueueComponentStart(component);
    }
}

bool GameObject::RemoveComponent(Component *component)
{
    if (!component) {
        return false;
    }

    if (dynamic_cast<Transform *>(component)) {
        INFLOG_WARN("Cannot remove Transform from GameObject '", m_name, "'");
        return false; // Cannot remove Transform
    }

    const auto blockers = GetRemovalBlockingComponentTypes(component);
    if (!blockers.empty()) {
        std::string blockerList;
        for (size_t i = 0; i < blockers.size(); ++i) {
            if (i > 0) {
                blockerList += ", ";
            }
            blockerList += blockers[i];
        }
        INFLOG_WARN("Cannot remove component '", component->GetTypeName(), "' from GameObject '", m_name,
                    "' because required by: ", blockerList);
        return false;
    }

    for (auto it = m_components.begin(); it != m_components.end(); ++it) {
        if (it->get() == component) {
            (*it)->CallOnDestroy();
            m_components.erase(it);
            return true;
        }
    }

    return false;
}

bool GameObject::CanRemoveComponent(Component *component) const
{
    return GetRemovalBlockingComponentTypes(component).empty();
}

std::vector<std::string> GameObject::GetRemovalBlockingComponentTypes(Component *component) const
{
    std::vector<std::string> blockers;
    if (!component)
        return blockers;

    // For every OTHER component on this GameObject, check if it declares a
    // requirement that only `component` satisfies.
    for (const auto &other : m_components) {
        if (other.get() == component)
            continue;

        const auto reqs = other->GetRequiredComponentTypes();
        for (const auto &req : reqs) {
            // Does the component being removed satisfy this requirement?
            if (!component->IsComponentType(req))
                continue;

            // It does — check whether any OTHER component also satisfies it.
            bool hasAlternative = false;
            for (const auto &c : m_components) {
                if (c.get() == component)
                    continue; // skip the one being removed
                if (c->IsComponentType(req)) {
                    hasAlternative = true;
                    break;
                }
            }
            if (!hasAlternative) {
                const std::string blockerType = other->GetTypeName();
                if (std::find(blockers.begin(), blockers.end(), blockerType) == blockers.end()) {
                    blockers.push_back(blockerType);
                }
                break;
            }
        }
    }
    return blockers;
}

void GameObject::AttachChild(std::unique_ptr<GameObject> child)
{
    if (!child)
        return;
    child->m_parent = this;
    // Propagate scene
    if (m_scene && child->m_scene != m_scene) {
        child->SetScene(m_scene);
    }
    m_children.push_back(std::move(child));
}

void GameObject::SetChildSiblingIndex(GameObject *child, int newIndex)
{
    int currentIndex = -1;
    for (size_t i = 0; i < m_children.size(); ++i) {
        if (m_children[i].get() == child) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }
    if (currentIndex < 0)
        return;
    newIndex = std::max(0, std::min(newIndex, static_cast<int>(m_children.size()) - 1));
    if (currentIndex == newIndex)
        return;
    auto ptr = std::move(m_children[currentIndex]);
    m_children.erase(m_children.begin() + currentIndex);
    m_children.insert(m_children.begin() + newIndex, std::move(ptr));
}

std::unique_ptr<GameObject> GameObject::DetachChild(GameObject *child)
{
    auto it = std::find_if(m_children.begin(), m_children.end(), [&](const auto &ptr) { return ptr.get() == child; });

    if (it != m_children.end()) {
        std::unique_ptr<GameObject> ret = std::move(*it);
        m_children.erase(it);
        if (ret)
            ret->m_parent = nullptr;
        return ret;
    }
    return nullptr;
}

GameObject *GameObject::FindChild(const std::string &name) const
{
    for (const auto &child : m_children) {
        if (child->GetName() == name) {
            return child.get();
        }
    }
    return nullptr;
}

GameObject *GameObject::FindDescendant(const std::string &name) const
{
    // First check direct children
    for (const auto &child : m_children) {
        if (child->GetName() == name) {
            return child.get();
        }
    }

    // Then search recursively
    for (const auto &child : m_children) {
        if (GameObject *found = child->FindDescendant(name)) {
            return found;
        }
    }

    return nullptr;
}

void GameObject::Update(float deltaTime)
{
    if (!m_active)
        return;

    // Update all components
    std::vector<Component *> components = GetComponentsInExecutionOrder();
    for (Component *comp : components) {
        if (comp && comp->IsEnabled()) {
            comp->Update(deltaTime);
        }
    }
}

void GameObject::FixedUpdate(float fixedDeltaTime)
{
    if (!m_active)
        return;

    std::vector<Component *> components = GetComponentsInExecutionOrder();
    for (Component *comp : components) {
        if (comp && comp->IsEnabled()) {
            comp->FixedUpdate(fixedDeltaTime);
        }
    }
}

void GameObject::LateUpdate(float deltaTime)
{
    if (!m_active)
        return;

    // LateUpdate all components
    std::vector<Component *> components = GetComponentsInExecutionOrder();
    for (Component *comp : components) {
        if (comp && comp->IsEnabled()) {
            comp->LateUpdate(deltaTime);
        }
    }
}

void GameObject::EditorUpdate(float deltaTime)
{
    if (!m_active)
        return;

    std::vector<Component *> components = GetComponentsInExecutionOrder();
    for (Component *comp : components) {
        if (!comp || !comp->IsEnabled())
            continue;
        if (!comp->WantsEditModeUpdate())
            continue;
        if (!comp->HasAwake() && comp->WantsEditModeLifecycle()) {
            comp->CallAwake();
        }
        if (comp->HasAwake()) {
            comp->Update(deltaTime);
        }
    }
}

std::string GameObject::Serialize() const
{
    json j;
    j["schema_version"] = 1;
    j["name"] = m_name;
    j["id"] = m_id;
    j["active"] = m_active;
    j["is_static"] = m_isStatic;
    j["tag"] = m_tag;
    j["layer"] = m_layer;

    // Serialize Transform
    j["transform"] = json::parse(m_transform.Serialize());

    // Serialize C++ components (excluding PyComponentProxy)
    json componentsArray = json::array();
    for (const auto &comp : m_components) {
        if (dynamic_cast<const PyComponentProxy *>(comp.get())) {
            continue; // PyComponentProxy serialized separately
        }
        json compJson = json::parse(comp->Serialize());
        componentsArray.push_back(compJson);
    }
    j["components"] = componentsArray;

    // Serialize PyComponentProxy (Python components) separately
    json pyComponentsArray = json::array();
    for (const auto &comp : m_components) {
        const PyComponentProxy *proxy = dynamic_cast<const PyComponentProxy *>(comp.get());
        if (proxy) {
            json pyCompJson = json::parse(proxy->Serialize());
            pyComponentsArray.push_back(pyCompJson);
        }
    }
    j["py_components"] = pyComponentsArray;

    // Serialize children
    json childrenArray = json::array();
    for (const auto &child : m_children) {
        json childJson = json::parse(child->Serialize());
        childrenArray.push_back(childJson);
    }
    j["children"] = childrenArray;

    return j.dump(2);
}

bool GameObject::Deserialize(const std::string &jsonStr)
{
    try {
        json j = json::parse(jsonStr);
        if (!j.contains("schema_version")) {
            INFLOG_ERROR("GameObject::Deserialize: missing required 'schema_version' field");
            return false;
        }

        // If attached to a scene, unregister current hierarchy before rebuilding
        if (m_scene) {
            std::vector<GameObject *> currentObjects;
            currentObjects.reserve(16);
            currentObjects.push_back(this);
            for (const auto &child : m_children) {
                child->CollectAllDescendants(currentObjects);
            }
            for (GameObject *obj : currentObjects) {
                if (obj) {
                    m_scene->UnregisterGameObject(obj->GetID());
                }
            }
        }

        // Clear existing children
        m_children.clear();

        // Destroy and clear existing components (except Transform)
        for (auto &comp : m_components) {
            comp->CallOnDestroy();
        }
        m_components.clear();

        // Basic properties
        if (j.contains("name")) {
            m_name = j["name"].get<std::string>();
        }
        if (j.contains("active")) {
            m_active = j["active"].get<bool>();
        }
        if (j.contains("is_static")) {
            m_isStatic = j["is_static"].get<bool>();
        }
        if (j.contains("tag")) {
            m_tag = j["tag"].get<std::string>();
        }
        if (j.contains("layer")) {
            m_layer = j["layer"].get<int>();
            if (m_layer < 0 || m_layer >= 32)
                m_layer = 0;
        }
        if (j.contains("id")) {
            uint64_t newId = j["id"].get<uint64_t>();
            if (newId != m_id) {
                m_id = newId;
                GameObject::EnsureNextID(m_id);
            }
        }

        // Transform
        if (j.contains("transform")) {
            std::string transformJson = j["transform"].dump();
            m_transform.Deserialize(transformJson);
        }

        // Components (recreate via factory)
        if (j.contains("components") && j["components"].is_array()) {
            for (const auto &compJson : j["components"]) {
                std::string typeName = compJson.value("type", std::string());
                if (typeName.empty() || typeName == "Transform") {
                    continue;
                }

                std::unique_ptr<Component> comp = ComponentFactory::Create(typeName);
                if (!comp) {
                    continue;
                }

                comp->SetGameObject(this);
                comp->Deserialize(compJson.dump());
                m_components.push_back(std::move(comp));
            }
        }

        // Python components are NOT restored here.
        // Scene::Deserialize stores them as PendingPyComponent for proper
        // Python-side reconstruction (via take_pending_py_components).

        // Children (full rebuild)
        if (j.contains("children") && j["children"].is_array()) {
            for (const auto &childJson : j["children"]) {
                auto child = std::make_unique<GameObject>("GameObject");
                child->m_scene = m_scene;
                child->m_parent = this;
                child->Deserialize(childJson.dump());
                m_children.push_back(std::move(child));
            }
        }

        // Re-register hierarchy in scene lookup
        if (m_scene) {
            std::vector<GameObject *> rebuilt;
            rebuilt.reserve(16);
            rebuilt.push_back(this);
            for (const auto &child : m_children) {
                child->CollectAllDescendants(rebuilt);
            }
            for (GameObject *obj : rebuilt) {
                if (obj) {
                    m_scene->RegisterGameObject(obj);
                }
            }
        }

        return true;
    } catch (const std::exception &e) {
        return false;
    }
}

void GameObject::CollectAllDescendants(std::vector<GameObject *> &out) const
{
    for (const auto &child : m_children) {
        out.push_back(child.get());
        child->CollectAllDescendants(out);
    }
}

} // namespace infengine
