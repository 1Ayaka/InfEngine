#pragma once

#include "EditorCameraController.h"
#include "Scene.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace infengine
{

// Forward declarations for component registries
class Collider;
class Rigidbody;
class MeshRenderer;

/**
 * @brief SceneManager - singleton that manages all scenes.
 *
 * Handles scene loading, switching, and provides access to the active scene.
 * In editor mode, it also manages the editor scene camera.
 */
class SceneManager
{
  public:
    // Singleton access
    static SceneManager &Instance();

    // Prevent copying
    SceneManager(const SceneManager &) = delete;
    SceneManager &operator=(const SceneManager &) = delete;

    // ========================================================================
    // Scene management
    // ========================================================================

    /// @brief Create a new empty scene
    Scene *CreateScene(const std::string &name);

    /// @brief Set the active scene
    void SetActiveScene(Scene *scene);

    /// @brief Get the currently active scene
    [[nodiscard]] Scene *GetActiveScene() const
    {
        return m_activeScene;
    }

    /// @brief Unload a scene
    void UnloadScene(Scene *scene);

    /// @brief Unload all scenes
    void UnloadAllScenes();

    /// @brief Get a scene by name
    [[nodiscard]] Scene *GetScene(const std::string &name) const;

    /// @brief Get all loaded scenes
    [[nodiscard]] const std::vector<std::unique_ptr<Scene>> &GetAllScenes() const
    {
        return m_scenes;
    }

    // ========================================================================
    // Frame update
    // ========================================================================

    /// @brief Call at the start of the game (after first scene loads)
    void Start();

    /// @brief Call every frame
    void Update(float deltaTime);

    /// @brief Called at a fixed time step (physics / deterministic logic)
    void FixedUpdate();

    /// @brief Call every frame after Update
    void LateUpdate(float deltaTime);

    /// @brief Process pending destroys at end of frame
    void EndFrame();

    // ========================================================================
    // DontDestroyOnLoad
    // ========================================================================

    /// @brief Mark a root GameObject so it survives scene switches.
    /// The object is moved to an internal persistent list owned by SceneManager.
    /// Unity: Object.DontDestroyOnLoad(gameObject)
    void DontDestroyOnLoad(GameObject *gameObject);

    /// @brief Get all persistent (DontDestroyOnLoad) objects
    [[nodiscard]] const std::vector<std::unique_ptr<GameObject>> &GetPersistentObjects() const
    {
        return m_persistentObjects;
    }

    // ========================================================================
    // Editor support
    // ========================================================================

    /// @brief Get the editor camera controller
    [[nodiscard]] EditorCameraController &GetEditorCameraController()
    {
        return m_editorCamera;
    }

    /// @brief Is the scene in play mode?
    [[nodiscard]] bool IsPlaying() const
    {
        return m_isPlaying;
    }

    /// @brief Enter play mode
    void Play();

    /// @brief Stop play mode
    void Stop();

    /// @brief Pause play mode
    void Pause();

    /// @brief Step exactly one frame while paused (Update + LateUpdate + EndFrame).
    /// Does nothing if not currently paused and playing.
    void Step(float deltaTime);

    [[nodiscard]] bool IsPaused() const
    {
        return m_isPaused;
    }

    /// @brief Get the fixed physics timestep in seconds.
    [[nodiscard]] float GetFixedTimeStep() const
    {
        return m_fixedTimeStep;
    }

    // ========================================================================
    // Callbacks
    // ========================================================================

    using SceneCallback = std::function<void(Scene *)>;

    void OnSceneLoaded(SceneCallback callback)
    {
        m_onSceneLoaded = callback;
    }
    void OnSceneUnloaded(SceneCallback callback)
    {
        m_onSceneUnloaded = callback;
    }

    // ========================================================================
    // Physics component registry — O(1) iteration, no GetAllObjects()
    // ========================================================================

    /// Register a Collider so physics sync iterates it directly.
    void RegisterCollider(Collider *collider);

    /// Unregister a Collider (e.g. OnDisable / destruction).
    void UnregisterCollider(Collider *collider);

    /// Register a Rigidbody so physics sync iterates it directly.
    void RegisterRigidbody(Rigidbody *rigidbody);

    /// Unregister a Rigidbody.
    void UnregisterRigidbody(Rigidbody *rigidbody);

    /// Clear all physics registry entries (called on scene unload / deserialize).
    void ClearPhysicsRegistry();

    /// Read-only access to the active colliders registry.
    [[nodiscard]] const std::vector<Collider *> &GetActiveColliders() const
    {
        return m_activeColliders;
    }

    /// Read-only access to the active rigidbodies registry.
    [[nodiscard]] const std::vector<Rigidbody *> &GetActiveRigidbodies() const
    {
        return m_activeRigidbodies;
    }

    // ========================================================================
    // MeshRenderer component registry — O(1) iteration for CollectRenderables
    // ========================================================================

    /// Register a MeshRenderer so rendering can iterate it directly.
    void RegisterMeshRenderer(MeshRenderer *renderer);

    /// Unregister a MeshRenderer (e.g. OnDisable / destruction).
    void UnregisterMeshRenderer(MeshRenderer *renderer);

    /// Read-only access to the active mesh renderers registry.
    [[nodiscard]] const std::vector<MeshRenderer *> &GetActiveMeshRenderers() const
    {
        return m_activeMeshRenderers;
    }

  private:
    SceneManager();
    ~SceneManager() = default;

    /// Walk all colliders in the active scene and sync transforms to Jolt.
    void SyncCollidersToPhysics();

    /// Force-sync ALL collider body positions to their current Transform,
    /// including dynamic bodies (which SyncCollidersToPhysics normally skips).
    /// Called once at the start of play to fix stale editor-mode positions.
    void ForceAllBodiesToCurrentTransform();

    /// Walk all Rigidbodies and write Jolt position/rotation back to Transform.
    void SyncRigidbodiesToTransform();

    /// Apply presentation interpolation for dynamic rigidbodies.
    void ApplyInterpolatedRigidbodies(float alpha);

    /// Detect user-driven Transform changes on dynamic Rigidbodies and teleport
    /// their Jolt bodies before the physics step.
    void SyncExternalRigidbodyMoves();

    std::vector<std::unique_ptr<Scene>> m_scenes;
    Scene *m_activeScene = nullptr;

    // Editor camera (exists even when no scene is loaded)
    std::unique_ptr<GameObject> m_editorCameraObject;
    Camera *m_editorCameraComponent = nullptr;
    EditorCameraController m_editorCamera;

    // Persistent objects (DontDestroyOnLoad)
    std::vector<std::unique_ptr<GameObject>> m_persistentObjects;

    // Fixed-update timing
    float m_fixedTimeStep = 1.0f / 50.0f; // 50 Hz default (Unity default)
    float m_fixedTimeAccumulator = 0.0f;
    float m_maxFixedDeltaTime = 0.1f; // cap to avoid spiral-of-death

    // Play mode state
    bool m_isPlaying = false;
    bool m_isPaused = false;

    // Callbacks
    SceneCallback m_onSceneLoaded;
    SceneCallback m_onSceneUnloaded;

    // Physics component registry — populated by Collider/Rigidbody OnEnable/OnDisable.
    // Avoids per-frame GetAllObjects() + dynamic_cast in sync methods.
    std::vector<Collider *> m_activeColliders;
    std::vector<Rigidbody *> m_activeRigidbodies;

    // MeshRenderer component registry — populated by MeshRenderer OnEnable/OnDisable.
    // Avoids per-frame GetAllObjects() + dynamic_cast in CollectRenderables.
    std::vector<MeshRenderer *> m_activeMeshRenderers;
};

} // namespace infengine
