#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace infengine
{

// Forward declarations
class GameObject;
class Transform;
class Collider;
struct CollisionInfo;

/**
 * @brief Base class for all components that can be attached to GameObjects.
 *
 * Follows Unity-style component lifecycle:
 * - Awake(): Called when component is created
 * - OnEnable(): Called when component becomes enabled
 * - Start(): Called before the first Update
 * - Update(): Called every frame
 * - LateUpdate(): Called after all Update calls
 * - OnDisable(): Called when component becomes disabled
 * - OnDestroy(): Called when component is destroyed
 * - OnValidate(): Called in editor when inspector values change
 * - Reset(): Called when component is first added or reset
 */
class Component
{
  public:
    Component();
    virtual ~Component() = default;

    // Non-copyable, movable
    Component(const Component &) = delete;
    Component &operator=(const Component &) = delete;
    Component(Component &&) = default;
    Component &operator=(Component &&) = default;

    // ========================================================================
    // Lifecycle methods (override in derived classes)
    // ========================================================================

    /// @brief Called when the component is first created
    virtual void Awake()
    {
    }

    /// @brief Called when the component becomes enabled
    virtual void OnEnable()
    {
    }

    /// @brief Called before the first Update, after all Awake calls
    virtual void Start()
    {
    }

    /// @brief Called every frame
    virtual void Update(float deltaTime)
    {
    }

    /// @brief Called at a fixed time step (physics / deterministic logic)
    virtual void FixedUpdate(float fixedDeltaTime)
    {
    }

    /// @brief Called every frame after all Update calls
    virtual void LateUpdate(float deltaTime)
    {
    }

    /// @brief Called when the component becomes disabled
    virtual void OnDisable()
    {
    }

    /// @brief Called when the component is being destroyed
    virtual void OnDestroy()
    {
    }

    /// @brief Called in editor when inspector values change (editor only)
    virtual void OnValidate()
    {
    }

    /// @brief Called when component is first added or reset in editor
    virtual void Reset()
    {
    }

    // ========================================================================
    // Physics callbacks (Unity-style collision/trigger events)
    // ========================================================================

    /// @brief Called when this collider/rigidbody begins touching another collider.
    virtual void OnCollisionEnter(const CollisionInfo &collision)
    {
        (void)collision;
    }

    /// @brief Called once per frame for every collider/rigidbody touching another collider.
    virtual void OnCollisionStay(const CollisionInfo &collision)
    {
        (void)collision;
    }

    /// @brief Called when this collider/rigidbody stops touching another collider.
    virtual void OnCollisionExit(const CollisionInfo &collision)
    {
        (void)collision;
    }

    /// @brief Called when another collider enters the trigger.
    virtual void OnTriggerEnter(Collider *other)
    {
        (void)other;
    }

    /// @brief Called once per frame for every collider inside the trigger.
    virtual void OnTriggerStay(Collider *other)
    {
        (void)other;
    }

    /// @brief Called when another collider exits the trigger.
    virtual void OnTriggerExit(Collider *other)
    {
        (void)other;
    }

    // ========================================================================
    // Internal lifecycle wrappers
    // ========================================================================

    /// @brief Call Awake once
    void CallAwake();

    /// @brief Call Start once
    void CallStart();

    /// @brief Call OnEnable
    void CallOnEnable();

    /// @brief Call OnDisable
    void CallOnDisable();

    /// @brief Call OnDestroy once.
    void CallOnDestroy();

    /// @brief Call OnValidate (editor only)
    void CallOnValidate();

    /// @brief Call Reset
    void CallReset();

    // ========================================================================
    // Accessors
    // ========================================================================

    /// @brief Get the GameObject this component is attached to
    [[nodiscard]] GameObject *GetGameObject() const
    {
        return m_gameObject;
    }

    /// @brief Get the Transform of the GameObject
    [[nodiscard]] Transform *GetTransform() const;

    /// @brief Get the stable component ID (generated at construction, preserved across serialization)
    [[nodiscard]] uint64_t GetComponentID() const
    {
        return m_componentId;
    }

    /// @brief Set component ID (used during deserialization to restore ID)
    void SetComponentID(uint64_t id);

    /// @brief Check if the component is enabled
    [[nodiscard]] bool IsEnabled() const
    {
        return m_enabled;
    }

    [[nodiscard]] bool HasAwake() const
    {
        return m_hasAwake;
    }

    [[nodiscard]] bool HasStarted() const
    {
        return m_hasStarted;
    }

    [[nodiscard]] bool IsDestroyed() const
    {
        return m_hasDestroyed;
    }

    /// @brief Enable or disable the component (triggers OnEnable/OnDisable)
    void SetEnabled(bool enabled);

    /// @brief Get per-component execution order (lower value runs earlier).
    [[nodiscard]] int GetExecutionOrder() const
    {
        return m_executionOrder;
    }

    /// @brief Set per-component execution order (Unity-style script order baseline).
    void SetExecutionOrder(int order)
    {
        m_executionOrder = order;
    }

    /// @brief Get component type name for serialization/debugging
    [[nodiscard]] virtual const char *GetTypeName() const
    {
        return "Component";
    }

    /// @brief Declare component types that this component depends on.
    ///
    /// When a component X is about to be removed, the engine checks every sibling
    /// component's GetRequiredComponentTypes(). If any entry matches X (via
    /// IsComponentType()) and no other sibling also satisfies that requirement,
    /// removal is blocked.
    ///
    /// Override in derived classes to declare dependencies — works identically
    /// to Unity's [RequireComponent] attribute.
    ///
    /// @return List of type-name strings (e.g. {"Collider"} for Rigidbody).
    [[nodiscard]] virtual std::vector<std::string> GetRequiredComponentTypes() const
    {
        return {};
    }

    /// @brief Check whether this component satisfies a given type name.
    ///
    /// Default returns true only when typeName exactly matches GetTypeName().
    /// Override in base classes that form a hierarchy (e.g. Collider) so that
    /// derived types (BoxCollider, SphereCollider …) also match the base name.
    [[nodiscard]] virtual bool IsComponentType(const std::string &typeName) const
    {
        return typeName == GetTypeName();
    }

    /// @brief Whether this component's lifecycle (Awake/OnEnable/OnDisable) should
    ///        fire in edit mode as well as play mode.
    ///        Pure C++ components (MeshRenderer, Collider, …) return true so that
    ///        their registry registration always works.
    ///        PyComponentProxy returns false to avoid running user Python code
    ///        (awake / on_enable) outside of play mode.
    [[nodiscard]] virtual bool WantsEditModeLifecycle() const
    {
        return true;
    }

    /// @brief Whether this component should receive Update() while in edit mode.
    /// Default false: runtime gameplay logic should not run outside play mode.
    [[nodiscard]] virtual bool WantsEditModeUpdate() const
    {
        return false;
    }

    /// @brief Whether this component wants physics callbacks dispatched.
    /// Default false to avoid per-contact dispatch overhead on components that
    /// do not implement collision/trigger behavior.
    [[nodiscard]] virtual bool WantsPhysicsCallbacks() const
    {
        return false;
    }

    // ========================================================================
    // Serialization
    // ========================================================================

    /// @brief Serialize component data to JSON string
    /// @return JSON string representation
    [[nodiscard]] virtual std::string Serialize() const;

    /// @brief Deserialize component data from JSON string
    /// @param jsonStr JSON string to deserialize from
    /// @return true if successful
    virtual bool Deserialize(const std::string &jsonStr);

  protected:
    friend class GameObject;
    friend class Camera;
    friend class MeshRenderer;
    friend class PyComponentProxy;

    /// @brief Set the owning GameObject (called by GameObject::AddComponent)
    void SetGameObject(GameObject *gameObject)
    {
        m_gameObject = gameObject;
    }

    GameObject *m_gameObject = nullptr;
    bool m_enabled = true;
    bool m_wasEnabled = false; // Track previous enabled state for OnEnable/OnDisable
    bool m_hasAwake = false;
    bool m_hasStarted = false;
    bool m_hasDestroyed = false;
    int m_executionOrder = 0;
    uint64_t m_componentId = 0;

  private:
    static uint64_t GenerateComponentID();
    static void EnsureNextComponentID(uint64_t id);

    friend class Scene; // Scene needs to call Start
};

} // namespace infengine
