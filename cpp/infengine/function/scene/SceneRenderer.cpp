#include "SceneRenderer.h"
#include "SceneManager.h"
#include <algorithm>

namespace infengine
{

// ============================================================================
// SceneRenderer Implementation
// ============================================================================

void SceneRenderer::PrepareFrame()
{
    // Always use editor camera for scene rendering (Scene View).
    // Game View handles its own camera independently via PrepareFrame(Camera*).
    SceneManager &sm = SceneManager::Instance();
    m_activeCamera = sm.GetEditorCameraController().GetCamera();

    // Collect and cull renderables (editor camera sees all layers)
    CollectRenderables(0xFFFFFFFF);

    if (m_frustumCulling) {
        PerformCulling();
    }

    SortRenderables();
}

void SceneRenderer::PrepareFrame(Camera *camera)
{
    if (!camera) {
        m_renderables.clear();
        m_visibleCount = 0;
        return;
    }

    m_activeCamera = camera;

    // Collect renderables filtered by camera's culling mask
    CollectRenderables(camera->GetCullingMask());

    if (m_frustumCulling) {
        PerformCulling();
    }

    SortRenderables();
}

glm::mat4 SceneRenderer::GetViewMatrix() const
{
    if (m_activeCamera) {
        return m_activeCamera->GetViewMatrix();
    }
    return glm::mat4{1.0f};
}

glm::mat4 SceneRenderer::GetProjectionMatrix() const
{
    if (m_activeCamera) {
        return m_activeCamera->GetProjectionMatrix();
    }
    return glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
}

glm::vec3 SceneRenderer::GetCameraPosition() const
{
    if (m_activeCamera && m_activeCamera->GetGameObject()) {
        return m_activeCamera->GetGameObject()->GetTransform()->GetWorldPosition();
    }
    return glm::vec3{0.0f, 0.0f, 5.0f};
}

glm::vec3 SceneRenderer::GetCameraForward() const
{
    if (m_activeCamera && m_activeCamera->GetGameObject()) {
        return m_activeCamera->GetGameObject()->GetTransform()->GetWorldForward();
    }
    return glm::vec3{0.0f, 0.0f, -1.0f};
}

glm::vec3 SceneRenderer::GetCameraUp() const
{
    if (m_activeCamera && m_activeCamera->GetGameObject()) {
        return m_activeCamera->GetGameObject()->GetTransform()->GetWorldUp();
    }
    return glm::vec3{0.0f, 1.0f, 0.0f};
}

void SceneRenderer::SetAspectRatio(float aspect)
{
    if (m_activeCamera) {
        m_activeCamera->SetAspectRatio(aspect);
    }
}

void SceneRenderer::CollectRenderables(uint32_t cullingMask)
{
    m_renderables.clear();

    Scene *activeScene = SceneManager::Instance().GetActiveScene();
    if (!activeScene)
        return;

    // Use the MeshRenderer registry — O(N) over active renderers only,
    // no GetAllObjects() tree walk, no dynamic_cast, no vector allocation.
    const auto &meshRenderers = SceneManager::Instance().GetActiveMeshRenderers();

    m_renderables.reserve(meshRenderers.size());

    for (MeshRenderer *renderer : meshRenderers) {
        if (!renderer || !renderer->IsEnabled())
            continue;

        GameObject *obj = renderer->GetGameObject();
        if (!obj || !obj->IsActiveInHierarchy())
            continue;

        // Layer-based culling: skip objects not in the camera's culling mask
        uint32_t objectLayerBit = 1u << static_cast<uint32_t>(obj->GetLayer());
        if ((cullingMask & objectLayerBit) == 0)
            continue;

        // Check if renderer has inline mesh or mesh reference
        if (!renderer->HasInlineMesh() && !renderer->GetMesh().IsValid())
            continue;

        RenderableObject renderable;
        renderable.objectId = obj->GetID();
        renderable.worldMatrix = obj->GetTransform()->GetWorldMatrix();
        renderable.mesh = renderer->GetMesh();
        renderable.materials = renderer->GetMaterials();
        renderable.renderMaterial = renderer->GetEffectiveMaterial(); // Get actual InfMaterial
        renderable.meshRenderer = renderer;                           // Store direct pointer

        // Get world-space bounding box for frustum culling
        glm::vec3 boundsMin, boundsMax;
        renderer->GetWorldBounds(boundsMin, boundsMax);
        renderable.worldBounds = AABB(boundsMin, boundsMax);
        renderable.visible = true; // Will be set by culling

        m_renderables.push_back(std::move(renderable));
    }

    m_visibleCount = m_renderables.size();
}

void SceneRenderer::PerformCulling()
{
    if (!m_activeCamera) {
        // No camera, mark all as visible
        m_visibleCount = m_renderables.size();
        return;
    }

    // Extract frustum from view-projection matrix
    glm::mat4 viewProj = m_activeCamera->GetViewProjectionMatrix();
    Frustum frustum;
    frustum.ExtractFromMatrix(viewProj);

    // Test each object against frustum
    m_visibleCount = 0;
    for (auto &renderable : m_renderables) {
        // Use AABB-frustum intersection test
        renderable.visible = frustum.IntersectsAABB(renderable.worldBounds);
        if (renderable.visible) {
            ++m_visibleCount;
        }
    }
}

void SceneRenderer::SortRenderables()
{
    // Sort by material render queue (for proper render order)
    // - Lower queue = render first (opaque before transparent)
    // - Within same queue, sort by material pointer (minimize state changes)

    std::sort(m_renderables.begin(), m_renderables.end(), [](const RenderableObject &a, const RenderableObject &b) {
        // Get render queues (default 2000 for opaque)
        int32_t queueA = a.renderMaterial ? a.renderMaterial->GetRenderQueue() : 2000;
        int32_t queueB = b.renderMaterial ? b.renderMaterial->GetRenderQueue() : 2000;

        if (queueA != queueB) {
            return queueA < queueB; // Lower queue first
        }

        // Same queue: sort by material pointer to minimize state changes
        return a.renderMaterial.get() < b.renderMaterial.get();
    });
}

DrawCallResult SceneRenderer::BuildDrawCalls() const
{
    DrawCallResult result;

    for (const auto &renderable : m_renderables) {
        MeshRenderer *renderer = renderable.meshRenderer;
        if (!renderer)
            continue;

        // Get mesh data from MeshRenderer
        if (renderer->HasInlineMesh()) {
            const auto &objVertices = renderer->GetInlineVertices();
            const auto &objIndices = renderer->GetInlineIndices();
            if (objVertices.empty() || objIndices.empty())
                continue;

            // Vertices stay in model/local space — GPU applies per-object model matrix via push constants
            const glm::mat4 &worldMatrix = renderable.worldMatrix;

            // Build DrawCall with per-object mesh pointers (Phase 2.3.4)
            // Include ALL objects (even frustum-culled ones) so shadow caster
            // passes can still draw them into the shadow map.
            DrawCall dc;
            dc.indexStart = 0; // Always 0 for per-object VBO
            dc.indexCount = static_cast<uint32_t>(objIndices.size());
            dc.worldMatrix = worldMatrix;
            dc.material = renderable.renderMaterial;
            dc.objectId = renderable.objectId;
            dc.frustumVisible = renderable.visible;
            dc.meshVertices = &objVertices;
            dc.meshIndices = &objIndices;
            result.drawCalls.push_back(dc);
        } else if (renderer->GetMesh().IsValid()) {
            // TODO: Load from resource system when implemented
            continue;
        } else {
            continue;
        }
    }

    return result;
}

// ============================================================================
// SceneRenderBridge Implementation
// ============================================================================

SceneRenderBridge &SceneRenderBridge::Instance()
{
    static SceneRenderBridge instance;
    return instance;
}

void SceneRenderBridge::UpdateCameraData(float *outPos, float *outLookAt, float *outUp)
{
    glm::vec3 pos = m_sceneRenderer.GetCameraPosition();
    glm::vec3 forward = m_sceneRenderer.GetCameraForward();
    glm::vec3 up = m_sceneRenderer.GetCameraUp();

    // LookAt is position + forward direction
    glm::vec3 lookAt = pos + forward;

    if (outPos) {
        outPos[0] = pos.x;
        outPos[1] = pos.y;
        outPos[2] = pos.z;
    }

    if (outLookAt) {
        outLookAt[0] = lookAt.x;
        outLookAt[1] = lookAt.y;
        outLookAt[2] = lookAt.z;
    }

    if (outUp) {
        outUp[0] = up.x;
        outUp[1] = up.y;
        outUp[2] = up.z;
    }
}

void SceneRenderBridge::OnWindowResize(uint32_t width, uint32_t height)
{
    if (height > 0) {
        float aspect = static_cast<float>(width) / static_cast<float>(height);
        m_sceneRenderer.SetAspectRatio(aspect);

        // Sync screen dimensions for ScreenToWorld/WorldToScreen
        Camera *editorCam = GetEditorCamera();
        if (editorCam) {
            editorCam->SetScreenDimensions(width, height);
        }
    }
}

void SceneRenderBridge::PrepareFrame()
{
    m_sceneRenderer.PrepareFrame();
}

DrawCallResult SceneRenderBridge::PrepareAndBuildForCamera(Camera *camera)
{
    // Use a temporary SceneRenderer for independent camera culling
    // so we don't disturb the editor camera's cached state.
    SceneRenderer tempRenderer;
    tempRenderer.SetFrustumCullingEnabled(m_sceneRenderer.IsFrustumCullingEnabled());
    tempRenderer.PrepareFrame(camera);
    return tempRenderer.BuildDrawCalls();
}

DrawCallResult SceneRenderBridge::BuildDrawCalls() const
{
    return m_sceneRenderer.BuildDrawCalls();
}

Camera *SceneRenderBridge::GetEditorCamera() const
{
    return SceneManager::Instance().GetEditorCameraController().GetCamera();
}

Camera *SceneRenderBridge::GetActiveCamera() const
{
    // For backward compat, returns the editor camera.
    return GetEditorCamera();
}

} // namespace infengine
