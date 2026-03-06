// Jolt types hidden behind opaque headers — no Jolt include needed
#include "SceneManager.h"
#include "Collider.h"
#include "EditorCameraController.h"
#include "GameObject.h"
#include "Rigidbody.h"
#include "physics/PhysicsECSStore.h"
#include "physics/PhysicsWorld.h"
#include <algorithm>
#include <function/audio/AudioEngine.h>

namespace infengine
{

SceneManager &SceneManager::Instance()
{
    static SceneManager instance;
    return instance;
}

SceneManager::SceneManager()
{
    // Create editor camera
    m_editorCameraObject = std::make_unique<GameObject>("Editor Camera");
    m_editorCameraComponent = m_editorCameraObject->AddComponent<Camera>();
    m_editorCamera.SetCamera(m_editorCameraComponent);
    m_editorCamera.Reset(); // Set default position
}

Scene *SceneManager::CreateScene(const std::string &name)
{
    auto scene = std::make_unique<Scene>(name);
    Scene *ptr = scene.get();
    m_scenes.push_back(std::move(scene));

    // If no active scene, make this one active
    if (!m_activeScene) {
        SetActiveScene(ptr);
    }

    if (m_onSceneLoaded) {
        m_onSceneLoaded(ptr);
    }

    return ptr;
}

void SceneManager::SetActiveScene(Scene *scene)
{
    m_activeScene = scene;
    // Note: We do NOT auto-assign the editor camera as mainCamera.
    // mainCamera == nullptr means "no game camera assigned" — the Game View
    // will show a placeholder. Scene View always uses the editor camera
    // via SceneRenderBridge / EditorCameraController, independent of mainCamera.
}

void SceneManager::UnloadScene(Scene *scene)
{
    if (!scene)
        return;

    if (m_onSceneUnloaded) {
        m_onSceneUnloaded(scene);
    }

    // If this was the active scene, clear registry and pointer
    if (m_activeScene == scene) {
        ClearPhysicsRegistry();
        m_activeScene = nullptr;
    }

    auto it = std::find_if(m_scenes.begin(), m_scenes.end(),
                           [scene](const std::unique_ptr<Scene> &s) { return s.get() == scene; });

    if (it != m_scenes.end()) {
        m_scenes.erase(it);
    }

    // Set a new active scene if available
    if (!m_activeScene && !m_scenes.empty()) {
        m_activeScene = m_scenes[0].get();
    }
}

void SceneManager::UnloadAllScenes()
{
    ClearPhysicsRegistry();

    for (auto &scene : m_scenes) {
        if (m_onSceneUnloaded) {
            m_onSceneUnloaded(scene.get());
        }
    }

    m_scenes.clear();
    m_activeScene = nullptr;
}

Scene *SceneManager::GetScene(const std::string &name) const
{
    for (const auto &scene : m_scenes) {
        if (scene->GetName() == name) {
            return scene.get();
        }
    }
    return nullptr;
}

void SceneManager::Start()
{
    if (m_activeScene) {
        m_activeScene->Start();
    }
}

void SceneManager::Update(float deltaTime)
{
    // Always update editor camera (for editor viewport navigation)
    m_editorCamera.Update(deltaTime);

    if (!m_isPlaying && m_activeScene) {
        m_activeScene->EditorUpdate(deltaTime);
    }

    // Update active scene if playing
    if (m_isPlaying && !m_isPaused && m_activeScene) {
        m_activeScene->ProcessPendingStarts();

        // ---- Fixed-update accumulator (Unity-style) ----
        float dt = std::min(deltaTime, m_maxFixedDeltaTime);
        m_fixedTimeAccumulator += dt;
        while (m_fixedTimeAccumulator >= m_fixedTimeStep) {
            // Detect user-driven Transform changes on dynamic Rigidbodies
            // and teleport their Jolt bodies before the physics step.
            SyncExternalRigidbodyMoves();

            // Sync collider transforms before physics step
            SyncCollidersToPhysics();

            m_activeScene->FixedUpdate(m_fixedTimeStep);

            // Step Jolt physics world
            PhysicsWorld::Instance().Step(m_fixedTimeStep);

            // Dispatch collision/trigger callbacks to components (Unity-style)
            PhysicsWorld::Instance().DispatchContactEvents();

            // Write physics results back to Transforms (dynamic Rigidbodies)
            SyncRigidbodiesToTransform();

            m_fixedTimeAccumulator -= m_fixedTimeStep;
        }

        ApplyInterpolatedRigidbodies(m_fixedTimeAccumulator / m_fixedTimeStep);

        m_activeScene->Update(deltaTime);
    }
}

void SceneManager::FixedUpdate()
{
    // Intentionally empty — fixed update is driven by the accumulator inside
    // Update() for correct time-step handling.  Exposed in the header so
    // external code *could* call it manually if needed, but normally it is
    // not called directly.
}

void SceneManager::LateUpdate(float deltaTime)
{
    if (m_isPlaying && !m_isPaused && m_activeScene) {
        m_activeScene->LateUpdate(deltaTime);
    }

    // Update spatial audio (runs even when paused so listener position stays synced)
    AudioEngine::Instance().Update(deltaTime);
}

void SceneManager::EndFrame()
{
    if (m_activeScene) {
        m_activeScene->ProcessPendingDestroys();
    }
}

void SceneManager::Play()
{
    // Only reset accumulator on initial play, not on resume-from-pause
    if (!m_isPlaying) {
        m_fixedTimeAccumulator = 0.0f;
    }

    m_isPlaying = true;
    m_isPaused = false;

    if (m_activeScene) {
        m_activeScene->SetPlaying(true);
        m_activeScene->Start();

        // Force-sync ALL body positions to current Transform.
        // Editor-mode bodies (created by EnsureSceneBodiesRegistered for picking)
        // may have stale positions if the user moved objects before pressing Play.
        ForceAllBodiesToCurrentTransform();

        // Ensure broad-phase tree is rebuilt after all Awake() calls
        // registered collider bodies, so raycasts work from the first frame.
        PhysicsWorld::Instance().OptimizeBroadPhase();
    }
}

void SceneManager::Stop()
{
    m_isPlaying = false;
    m_isPaused = false;
    m_fixedTimeAccumulator = 0.0f;

    if (m_activeScene) {
        m_activeScene->SetPlaying(false);
    }

    // Scene state restore is handled by Python PlayModeManager
    // (serialize on Play, deserialize on Stop)
}

void SceneManager::Pause()
{
    m_isPaused = !m_isPaused;
}

void SceneManager::Step(float deltaTime)
{
    if (!m_isPaused || !m_isPlaying || !m_activeScene)
        return;

    m_activeScene->ProcessPendingStarts();

    // Detect external moves before stepping physics
    SyncExternalRigidbodyMoves();
    SyncCollidersToPhysics();
    m_activeScene->FixedUpdate(m_fixedTimeStep);
    PhysicsWorld::Instance().Step(m_fixedTimeStep);
    PhysicsWorld::Instance().DispatchContactEvents();
    SyncRigidbodiesToTransform();
    ApplyInterpolatedRigidbodies(1.0f);
    m_activeScene->Update(deltaTime);
    m_activeScene->LateUpdate(deltaTime);
    m_activeScene->ProcessPendingDestroys();
}

void SceneManager::DontDestroyOnLoad(GameObject *gameObject)
{
    if (!gameObject)
        return;

    // Must be a root object (no parent) for DontDestroyOnLoad
    if (gameObject->GetParent() != nullptr) {
        // Walk up to root
        GameObject *root = gameObject;
        while (root->GetParent()) {
            root = root->GetParent();
        }
        gameObject = root;
    }

    // Detach from current scene
    Scene *scene = gameObject->GetScene();
    if (!scene)
        return;

    auto owned = scene->DetachRootObject(gameObject);
    if (!owned)
        return;

    // Move to persistent list
    owned->SetScene(nullptr); // No longer belongs to any scene
    m_persistentObjects.push_back(std::move(owned));
}

void SceneManager::SyncCollidersToPhysics()
{
    auto handles = PhysicsECSStore::Instance().GetAliveColliderHandles();
    for (auto handle : handles) {
        auto &data = PhysicsECSStore::Instance().GetCollider(handle);
        auto *col = data.owner;
        if (!col || !col->IsEnabled())
            continue;
        auto *go = col->GetGameObject();
        if (!go || go->GetScene() != m_activeScene)
            continue;
        col->SyncTransformToPhysics();
    }
}

void SceneManager::ForceAllBodiesToCurrentTransform()
{
    auto &pw = PhysicsWorld::Instance();
    if (!pw.IsInitialized())
        return;

    for (auto *col : m_activeColliders) {
        if (col->GetBodyId() == 0xFFFFFFFF)
            continue;

        auto *go = col->GetGameObject();
        if (!go)
            continue;

        Transform *tf = go->GetTransform();
        if (!tf)
            continue;

        glm::quat rot = tf->GetWorldRotation();
        glm::vec3 worldCenter = rot * col->GetCenter();
        glm::vec3 pos = tf->GetPosition() + worldCenter;
        pw.SetBodyPosition(col->GetBodyId(), pos, rot);
    }
}

void SceneManager::SyncRigidbodiesToTransform()
{
    auto handles = PhysicsECSStore::Instance().GetAliveRigidbodyHandles();
    for (auto handle : handles) {
        auto &data = PhysicsECSStore::Instance().GetRigidbody(handle);
        auto *rb = data.owner;
        if (!rb || !rb->IsEnabled())
            continue;
        auto *go = rb->GetGameObject();
        if (!go || go->GetScene() != m_activeScene)
            continue;
        rb->SyncPhysicsToTransform();
    }
}

void SceneManager::ApplyInterpolatedRigidbodies(float alpha)
{
    if (!m_activeScene)
        return;

    auto handles = PhysicsECSStore::Instance().GetAliveRigidbodyHandles();
    for (auto handle : handles) {
        auto &data = PhysicsECSStore::Instance().GetRigidbody(handle);
        auto *rb = data.owner;
        if (!rb || !rb->IsEnabled())
            continue;
        auto *go = rb->GetGameObject();
        if (!go || go->GetScene() != m_activeScene)
            continue;
        rb->ApplyInterpolatedTransform(alpha);
    }
}

void SceneManager::SyncExternalRigidbodyMoves()
{
    auto handles = PhysicsECSStore::Instance().GetAliveRigidbodyHandles();
    for (auto handle : handles) {
        auto &data = PhysicsECSStore::Instance().GetRigidbody(handle);
        auto *rb = data.owner;
        if (!rb || !rb->IsEnabled())
            continue;
        auto *go = rb->GetGameObject();
        if (!go || go->GetScene() != m_activeScene)
            continue;
        rb->SyncExternalMovesToPhysics();
    }
}

// ============================================================================
// Physics component registry
// ============================================================================

void SceneManager::RegisterCollider(Collider *collider)
{
    if (!collider)
        return;
    // Avoid duplicates (OnEnable may be called more than once)
    for (auto *c : m_activeColliders) {
        if (c == collider)
            return;
    }
    m_activeColliders.push_back(collider);
}

void SceneManager::UnregisterCollider(Collider *collider)
{
    // Swap-and-pop for O(1) removal
    for (size_t i = 0; i < m_activeColliders.size(); ++i) {
        if (m_activeColliders[i] == collider) {
            m_activeColliders[i] = m_activeColliders.back();
            m_activeColliders.pop_back();
            return;
        }
    }
}

void SceneManager::RegisterRigidbody(Rigidbody *rigidbody)
{
    if (!rigidbody)
        return;
    for (auto *r : m_activeRigidbodies) {
        if (r == rigidbody)
            return;
    }
    m_activeRigidbodies.push_back(rigidbody);
}

void SceneManager::UnregisterRigidbody(Rigidbody *rigidbody)
{
    for (size_t i = 0; i < m_activeRigidbodies.size(); ++i) {
        if (m_activeRigidbodies[i] == rigidbody) {
            m_activeRigidbodies[i] = m_activeRigidbodies.back();
            m_activeRigidbodies.pop_back();
            return;
        }
    }
}

void SceneManager::ClearPhysicsRegistry()
{
    m_activeColliders.clear();
    m_activeRigidbodies.clear();
    m_activeMeshRenderers.clear();
}

// ========================================================================
// MeshRenderer component registry
// ========================================================================

void SceneManager::RegisterMeshRenderer(MeshRenderer *renderer)
{
    if (!renderer)
        return;
    // Avoid duplicates (OnEnable may be called more than once)
    for (auto *r : m_activeMeshRenderers) {
        if (r == renderer)
            return;
    }
    m_activeMeshRenderers.push_back(renderer);
}

void SceneManager::UnregisterMeshRenderer(MeshRenderer *renderer)
{
    // Swap-and-pop for O(1) removal
    for (size_t i = 0; i < m_activeMeshRenderers.size(); ++i) {
        if (m_activeMeshRenderers[i] == renderer) {
            m_activeMeshRenderers[i] = m_activeMeshRenderers.back();
            m_activeMeshRenderers.pop_back();
            return;
        }
    }
}

} // namespace infengine
