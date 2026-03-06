/**
 * @file ScenePicker.cpp
 * @brief InfEngine — Raycast helpers and scene object picking
 *
 * Split from InfEngine.cpp for maintainability.
 * Contains: RayIntersectsAABB, RayIntersectsTriangle, RayIntersectsMesh,
 *           InfEngine::PickSceneObjectId (incl. gizmo arrows),
 *           InfEngine::SetEditorToolHighlight, InfEngine::ScreenToWorldRay.
 */

#include "InfEngine.h"

#include <algorithm>
#include <cmath>
#include <function/renderer/EditorTools.h>
#include <function/renderer/GizmosDrawCallBuffer.h>
#include <function/renderer/InfRenderer.h>
#include <function/scene/MeshRenderer.h>
#include <function/scene/SceneRenderer.h>
#include <function/scene/physics/PhysicsWorld.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <tuple>
#include <unordered_set>

namespace infengine
{

// ----------------------------------
// Raycast helpers
// ----------------------------------

static bool RayIntersectsAABB(const glm::vec3 &origin, const glm::vec3 &direction, const glm::vec3 &minBounds,
                              const glm::vec3 &maxBounds, float &outDistance)
{
    float tmin = 0.0f;
    float tmax = std::numeric_limits<float>::max();

    for (int i = 0; i < 3; ++i) {
        const float dir = direction[i];
        const float originAxis = origin[i];
        const float minAxis = minBounds[i];
        const float maxAxis = maxBounds[i];

        if (std::abs(dir) < 1e-6f) {
            if (originAxis < minAxis || originAxis > maxAxis) {
                return false;
            }
            continue;
        }

        const float invDir = 1.0f / dir;
        float t1 = (minAxis - originAxis) * invDir;
        float t2 = (maxAxis - originAxis) * invDir;

        if (t1 > t2) {
            std::swap(t1, t2);
        }

        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);

        if (tmin > tmax) {
            return false;
        }
    }

    outDistance = (tmin >= 0.0f) ? tmin : tmax;
    return outDistance >= 0.0f;
}

// Möller–Trumbore ray-triangle intersection
static bool RayIntersectsTriangle(const glm::vec3 &origin, const glm::vec3 &direction, const glm::vec3 &v0,
                                  const glm::vec3 &v1, const glm::vec3 &v2, float &outDistance)
{
    const float EPSILON = 1e-7f;
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 h = glm::cross(direction, edge2);
    float a = glm::dot(edge1, h);

    if (a > -EPSILON && a < EPSILON) {
        return false; // Ray is parallel to triangle
    }

    float f = 1.0f / a;
    glm::vec3 s = origin - v0;
    float u = f * glm::dot(s, h);

    if (u < 0.0f || u > 1.0f) {
        return false;
    }

    glm::vec3 q = glm::cross(s, edge1);
    float v = f * glm::dot(direction, q);

    if (v < 0.0f || u + v > 1.0f) {
        return false;
    }

    float t = f * glm::dot(edge2, q);
    if (t > EPSILON) {
        outDistance = t;
        return true;
    }

    return false;
}

// Test ray against mesh triangles
static bool RayIntersectsMesh(const glm::vec3 &origin, const glm::vec3 &direction, const std::vector<Vertex> &vertices,
                              const std::vector<uint32_t> &indices, const glm::mat4 &worldMatrix, float &outDistance)
{
    float closestHit = std::numeric_limits<float>::max();
    bool hit = false;

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        // Transform vertices to world space
        glm::vec4 v0_local(vertices[indices[i]].pos, 1.0f);
        glm::vec4 v1_local(vertices[indices[i + 1]].pos, 1.0f);
        glm::vec4 v2_local(vertices[indices[i + 2]].pos, 1.0f);

        glm::vec3 v0 = glm::vec3(worldMatrix * v0_local);
        glm::vec3 v1 = glm::vec3(worldMatrix * v1_local);
        glm::vec3 v2 = glm::vec3(worldMatrix * v2_local);

        float t;
        if (RayIntersectsTriangle(origin, direction, v0, v1, v2, t)) {
            if (t < closestHit) {
                closestHit = t;
                hit = true;
            }
        }
    }

    if (hit) {
        outDistance = closestHit;
    }
    return hit;
}

// ----------------------------------
// Scene Picking
// ----------------------------------

uint64_t InfEngine::PickSceneObjectId(float screenX, float screenY, float viewportWidth, float viewportHeight)
{
    if (!CheckEngineValid("pick scene object")) {
        return 0;
    }

    if (viewportWidth <= 0.0f || viewportHeight <= 0.0f) {
        return 0;
    }

    Scene *scene = SceneManager::Instance().GetActiveScene();
    if (!scene) {
        return 0;
    }

    Camera *camera = SceneRenderBridge::Instance().GetActiveCamera();
    if (!camera) {
        return 0;
    }

    // =========================================================================
    // Screen to Ray conversion — delegate to Camera::ScreenPointToRay
    // =========================================================================
    auto [rayOrigin, rayDirection] =
        camera->ScreenPointToRay(glm::vec2(screenX, screenY), viewportWidth, viewportHeight);

    // =========================================================================
    // Phase 0: Physics raycast (uses Jolt colliders for fast picking)
    // =========================================================================
    // If any objects have Collider components, the physics raycast is the
    // primary picking method (much faster than per-triangle mesh tests).
    // Objects without colliders fall through to the legacy mesh-based Phase 1/2.
    //
    // In editor mode, colliders may not have Awake() called yet, so we
    // ensure bodies are registered and transforms are synced before querying.
    uint64_t physicsPickedId = 0;
    float physicsPickDistance = std::numeric_limits<float>::max();

    if (PhysicsWorld::Instance().IsInitialized()) {
        // Ensure all colliders in the scene have bodies & up-to-date transforms
        PhysicsWorld::Instance().EnsureSceneBodiesRegistered(scene);

        RaycastHit hit;
        if (PhysicsWorld::Instance().Raycast(rayOrigin, rayDirection, 10000.0f, hit)) {
            if (hit.gameObject) {
                physicsPickedId = hit.gameObject->GetID();
                physicsPickDistance = hit.distance;
            }
        }
    }

    // Collect candidates that pass AABB test, then do precise triangle test
    struct PickCandidate
    {
        GameObject *obj;
        MeshRenderer *renderer;
        float aabbDistance;
    };
    std::vector<PickCandidate> candidates;

    // Phase 1: AABB culling - collect potential candidates
    // Use the MeshRenderer registry instead of FindObjectsWithComponent (no tree walk / dynamic_cast).
    const auto &meshRenderers = SceneManager::Instance().GetActiveMeshRenderers();
    for (MeshRenderer *renderer : meshRenderers) {
        if (!renderer || !renderer->IsEnabled()) {
            continue;
        }

        GameObject *obj = renderer->GetGameObject();
        if (!obj || !obj->IsActiveInHierarchy()) {
            continue;
        }

        if (!renderer->HasInlineMesh() && !renderer->GetMesh().IsValid()) {
            continue;
        }

        glm::vec3 boundsMin, boundsMax;
        renderer->GetWorldBounds(boundsMin, boundsMax);

        float aabbDist = 0.0f;
        if (RayIntersectsAABB(rayOrigin, rayDirection, boundsMin, boundsMax, aabbDist)) {
            candidates.push_back({obj, renderer, aabbDist});
        }
    }

    // Sort by AABB distance for early-out optimization
    std::sort(candidates.begin(), candidates.end(),
              [](const PickCandidate &a, const PickCandidate &b) { return a.aabbDistance < b.aabbDistance; });

    // Phase 2: Precise triangle test on candidates
    float closestDistance = std::numeric_limits<float>::max();
    uint64_t pickedId = 0;

    // Seed with physics raycast result (if any)
    if (physicsPickedId != 0) {
        closestDistance = physicsPickDistance;
        pickedId = physicsPickedId;
    }

    for (const auto &candidate : candidates) {
        // Early out if AABB distance is already farther than closest hit
        if (candidate.aabbDistance > closestDistance) {
            break;
        }

        MeshRenderer *renderer = candidate.renderer;
        if (renderer->HasInlineMesh()) {
            const auto &vertices = renderer->GetInlineVertices();
            const auto &indices = renderer->GetInlineIndices();
            glm::mat4 worldMatrix = candidate.obj->GetTransform()->GetWorldMatrix();

            float meshDist = 0.0f;
            if (RayIntersectsMesh(rayOrigin, rayDirection, vertices, indices, worldMatrix, meshDist)) {
                if (meshDist < closestDistance) {
                    closestDistance = meshDist;
                    pickedId = candidate.obj->GetID();
                }
            }
        } else {
            // For non-inline meshes, fall back to AABB (mesh data not available here)
            // This is a limitation - ideally we'd access the mesh vertices from resource system
            if (candidate.aabbDistance < closestDistance) {
                closestDistance = candidate.aabbDistance;
                pickedId = candidate.obj->GetID();
            }
        }
    }

    // =========================================================================
    // Phase 3: Gizmo proximity test
    // =========================================================================
    // For Translate / Scale modes: ray-to-axis-LINE proximity (segment test).
    // For Rotate mode: ray-to-CIRCLE proximity (ring test).
    // For Scale mode: axis directions are rotated by the object's world
    // rotation so the gizmo aligns with local axes (like Unity).
    //
    // Gizmos render with depth-test disabled (always on top), so gizmo picks
    // have ABSOLUTE PRIORITY over scene objects.

    if (m_renderer) {
        EditorTools *tools = m_renderer->GetEditorTools();
        EditorTools::ToolMode toolMode = tools ? tools->GetToolMode() : EditorTools::ToolMode::None;
        bool phase3Active = tools && toolMode != EditorTools::ToolMode::None && m_selectedObjectId != 0;

        // Throttled Phase 3 diagnostics (once per ~2 sec at 60fps hover)
        static int p3Counter = 0;
        bool p3Log = (++p3Counter % 120 == 1);

        if (p3Log) {
            INFLOG_DEBUG("[Phase3] active=", phase3Active, " selObjId=", m_selectedObjectId,
                         " hasTools=", (tools != nullptr), " toolMode=", (tools ? static_cast<int>(toolMode) : -1));
        }

        if (phase3Active) {
            // Look up selected object position + constant-size scale
            GameObject *selObj = scene->FindByID(m_selectedObjectId);
            if (selObj && selObj->IsActiveInHierarchy() && selObj->GetTransform()) {
                Transform *selTransform = selObj->GetTransform();
                glm::vec3 objPos = selTransform->GetPosition();
                float camDist = glm::length(rayOrigin - objPos);
                float scale = camDist * 0.15f * tools->GetHandleSize();
                if (scale < 0.01f)
                    scale = 0.01f;

                // Full arrow length in local space: shaft(0.8) + cone(0.2) = 1.0
                float arrowLen = 1.0f * scale;

                // World-space click threshold: ~12 screen pixels converted to world units
                // at the gizmo's depth.  worldThreshold ≈ pixelThreshold * worldPerPixel
                constexpr float PIXEL_THRESHOLD = 12.0f;
                float tanHalfFov = std::tan(glm::radians(camera->GetFieldOfView()) * 0.5f);
                float worldPerPixel = (2.0f * camDist * tanHalfFov) / viewportHeight;
                float worldThreshold = PIXEL_THRESHOLD * worldPerPixel;

                static const glm::vec3 WORLD_AXIS_DIRS[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
                static const uint64_t AXIS_IDS[3] = {EditorTools::X_AXIS_ID, EditorTools::Y_AXIS_ID,
                                                     EditorTools::Z_AXIS_ID};

                // In local mode, rotate axis directions by object's world rotation
                glm::vec3 localAxisDirs[3];
                const glm::vec3 *axisDirs = WORLD_AXIS_DIRS;
                if (tools->GetLocalMode()) {
                    glm::quat worldRot = selTransform->GetWorldRotation();
                    localAxisDirs[0] = worldRot * WORLD_AXIS_DIRS[0];
                    localAxisDirs[1] = worldRot * WORLD_AXIS_DIRS[1];
                    localAxisDirs[2] = worldRot * WORLD_AXIS_DIRS[2];
                    axisDirs = localAxisDirs;
                }

                float bestDist = worldThreshold; // must beat threshold
                uint64_t gizmoPickedId = 0;

                float debugDists[3] = {0, 0, 0}; // for logging

                if (toolMode == EditorTools::ToolMode::Rotate) {
                    // =============================================================
                    // ROTATE MODE: Ray-to-circle proximity test
                    // =============================================================
                    // Each ring is a circle with:
                    //   centre = objPos, normal = axis direction,
                    //   radius = majorRadius * scale  (majorRadius = 0.85)
                    constexpr float MAJOR_RADIUS = 0.85f;
                    float ringRadius = MAJOR_RADIUS * scale;
                    // Slightly larger threshold for rings (they're thin torus tubes)
                    float ringThreshold = worldThreshold * 1.2f;
                    bestDist = ringThreshold;

                    for (int ai = 0; ai < 3; ++ai) {
                        glm::vec3 normal = axisDirs[ai]; // world-aligned or local, depending on mode
                        float dDotN = glm::dot(rayDirection, normal);

                        // Find parameter along ray closest to the ring's plane
                        float tPlane;
                        if (std::abs(dDotN) > 1e-6f) {
                            tPlane = glm::dot(objPos - rayOrigin, normal) / dDotN;
                        } else {
                            // Ray nearly parallel to ring plane — closest point to centre
                            tPlane = glm::dot(objPos - rayOrigin, rayDirection);
                        }
                        if (tPlane < 0.0f)
                            tPlane = 0.0f; // behind camera

                        glm::vec3 P = rayOrigin + rayDirection * tPlane;

                        // Project P onto the ring's plane
                        glm::vec3 toP = P - objPos;
                        glm::vec3 toPinPlane = toP - glm::dot(toP, normal) * normal;

                        float lenInPlane = glm::length(toPinPlane);
                        glm::vec3 Q; // closest point on ring
                        if (lenInPlane < 1e-8f) {
                            // P is at centre — pick any point on ring
                            glm::vec3 arbitrary = (std::abs(normal.x) < 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                            glm::vec3 perp = glm::normalize(glm::cross(normal, arbitrary));
                            Q = objPos + perp * ringRadius;
                        } else {
                            Q = objPos + (toPinPlane / lenInPlane) * ringRadius;
                        }

                        // Distance from ring closest-point Q to the ray
                        float tQ = glm::dot(Q - rayOrigin, rayDirection);
                        if (tQ < 0.0f)
                            tQ = 0.0f;
                        glm::vec3 closestOnRay = rayOrigin + rayDirection * tQ;
                        float dist = glm::length(closestOnRay - Q);
                        debugDists[ai] = dist;

                        if (dist < bestDist) {
                            bestDist = dist;
                            gizmoPickedId = AXIS_IDS[ai];
                        }
                    }
                } else {
                    // =============================================================
                    // TRANSLATE / SCALE MODE: Ray-to-line-segment proximity test
                    // =============================================================
                    for (int ai = 0; ai < 3; ++ai) {
                        glm::vec3 axisDir = axisDirs[ai];
                        glm::vec3 segStart = objPos;
                        glm::vec3 segEnd = objPos + axisDir * arrowLen;

                        // Closest-points-between-two-lines formula
                        glm::vec3 w = rayOrigin - segStart;
                        float a = glm::dot(rayDirection, rayDirection); // always 1
                        float b = glm::dot(rayDirection, axisDir);
                        float c = glm::dot(axisDir, axisDir); // ~1
                        float d = glm::dot(rayDirection, w);
                        float e = glm::dot(axisDir, w);

                        float denom = a * c - b * b;
                        float t, s;
                        if (std::abs(denom) < 1e-8f) {
                            t = 0.0f;
                            s = e / c;
                        } else {
                            t = (b * e - c * d) / denom;
                            s = (a * e - b * d) / denom;
                        }

                        // Clamp s to segment [0, arrowLen]
                        s = std::max(0.0f, std::min(s, arrowLen));
                        t = glm::dot((segStart + axisDir * s) - rayOrigin, rayDirection);

                        if (t < 0.0f)
                            t = 0.0f;

                        glm::vec3 closestOnRay = rayOrigin + rayDirection * t;
                        glm::vec3 closestOnAxis = segStart + axisDir * s;

                        float dist = glm::length(closestOnRay - closestOnAxis);
                        debugDists[ai] = dist;

                        if (dist < bestDist) {
                            bestDist = dist;
                            gizmoPickedId = AXIS_IDS[ai];
                        }
                    }
                }

                if (p3Log) {
                    INFLOG_DEBUG("[Phase3] objPos=(", objPos.x, ",", objPos.y, ",", objPos.z, ")", " camDist=", camDist,
                                 " scale=", scale, " arrowLen=", arrowLen, " threshold=", worldThreshold);
                    INFLOG_DEBUG("[Phase3] axisDist X=", debugDists[0], " Y=", debugDists[1], " Z=", debugDists[2],
                                 " bestDist=", bestDist, " pickedId=", gizmoPickedId);
                }

                if (gizmoPickedId != 0) {
                    return gizmoPickedId;
                }
            }
        }
    }

    // =========================================================================
    // Phase 4: Component icon picking (Unity-style clickable icon billboards)
    // =========================================================================
    // Ray-to-point proximity test against each icon position.
    // Icons that are closer to the camera than any mesh hit win.
    // Icon picks override scene mesh picks but NOT gizmo tool handles (Phase 3).

    if (m_renderer) {
        GizmosDrawCallBuffer *buf = m_renderer->GetGizmosDrawCallBuffer();
        if (buf && buf->HasIconData()) {
            const auto &icons = buf->GetIconEntries();
            for (const auto &icon : icons) {
                // Closest point on the ray to the icon center
                float t = glm::dot(icon.position - rayOrigin, rayDirection);
                if (t < 0.0f)
                    continue; // behind camera

                glm::vec3 closestOnRay = rayOrigin + rayDirection * t;
                float dist = glm::length(closestOnRay - icon.position);

                // Icon's world-space radius (same angular size as rendering)
                float camDist = glm::length(icon.position - rayOrigin);
                float iconRadius = camDist * GizmosDrawCallBuffer::ICON_SIZE_FACTOR;

                if (dist < iconRadius && t < closestDistance) {
                    closestDistance = t;
                    pickedId = icon.objectId;
                }
            }
        }
    }

    return pickedId;
}

std::vector<uint64_t> InfEngine::PickSceneObjectIds(float screenX, float screenY, float viewportWidth,
                                                    float viewportHeight)
{
    std::vector<uint64_t> orderedIds;

    if (!CheckEngineValid("pick scene objects")) {
        return orderedIds;
    }

    if (viewportWidth <= 0.0f || viewportHeight <= 0.0f) {
        return orderedIds;
    }

    Scene *scene = SceneManager::Instance().GetActiveScene();
    if (!scene) {
        return orderedIds;
    }

    Camera *camera = SceneRenderBridge::Instance().GetActiveCamera();
    if (!camera) {
        return orderedIds;
    }

    auto [rayOrigin, rayDirection] =
        camera->ScreenPointToRay(glm::vec2(screenX, screenY), viewportWidth, viewportHeight);

    std::vector<std::pair<float, uint64_t>> hits;
    hits.reserve(64);

    // Physics candidates (fast broad candidate source)
    if (PhysicsWorld::Instance().IsInitialized()) {
        PhysicsWorld::Instance().EnsureSceneBodiesRegistered(scene);
        std::vector<RaycastHit> physicsHits = PhysicsWorld::Instance().RaycastAll(rayOrigin, rayDirection, 10000.0f);
        for (const RaycastHit &hit : physicsHits) {
            if (hit.gameObject) {
                hits.emplace_back(hit.distance, hit.gameObject->GetID());
            }
        }
    }

    // Mesh candidates (precise inline mesh / AABB fallback)
    struct PickCandidate
    {
        GameObject *obj;
        MeshRenderer *renderer;
        float aabbDistance;
    };
    std::vector<PickCandidate> candidates;

    const auto &meshRenderers = SceneManager::Instance().GetActiveMeshRenderers();
    for (MeshRenderer *renderer : meshRenderers) {
        if (!renderer || !renderer->IsEnabled()) {
            continue;
        }

        GameObject *obj = renderer->GetGameObject();
        if (!obj || !obj->IsActiveInHierarchy()) {
            continue;
        }

        if (!renderer->HasInlineMesh() && !renderer->GetMesh().IsValid()) {
            continue;
        }

        glm::vec3 boundsMin, boundsMax;
        renderer->GetWorldBounds(boundsMin, boundsMax);

        float aabbDist = 0.0f;
        if (RayIntersectsAABB(rayOrigin, rayDirection, boundsMin, boundsMax, aabbDist)) {
            candidates.push_back({obj, renderer, aabbDist});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const PickCandidate &a, const PickCandidate &b) { return a.aabbDistance < b.aabbDistance; });

    for (const auto &candidate : candidates) {
        MeshRenderer *renderer = candidate.renderer;
        if (renderer->HasInlineMesh()) {
            const auto &vertices = renderer->GetInlineVertices();
            const auto &indices = renderer->GetInlineIndices();
            glm::mat4 worldMatrix = candidate.obj->GetTransform()->GetWorldMatrix();

            float meshDist = 0.0f;
            if (RayIntersectsMesh(rayOrigin, rayDirection, vertices, indices, worldMatrix, meshDist)) {
                hits.emplace_back(meshDist, candidate.obj->GetID());
            }
        } else {
            hits.emplace_back(candidate.aabbDistance, candidate.obj->GetID());
        }
    }

    // Icon candidates (component icon billboards)
    if (m_renderer) {
        GizmosDrawCallBuffer *buf = m_renderer->GetGizmosDrawCallBuffer();
        if (buf && buf->HasIconData()) {
            const auto &icons = buf->GetIconEntries();
            for (const auto &icon : icons) {
                float t = glm::dot(icon.position - rayOrigin, rayDirection);
                if (t < 0.0f)
                    continue;

                glm::vec3 closestOnRay = rayOrigin + rayDirection * t;
                float dist = glm::length(closestOnRay - icon.position);

                float camDist = glm::length(icon.position - rayOrigin);
                float iconRadius = camDist * GizmosDrawCallBuffer::ICON_SIZE_FACTOR;

                if (dist < iconRadius) {
                    hits.emplace_back(t, icon.objectId);
                }
            }
        }
    }

    if (hits.empty()) {
        return orderedIds;
    }

    std::sort(hits.begin(), hits.end(), [](const std::pair<float, uint64_t> &a, const std::pair<float, uint64_t> &b) {
        return a.first < b.first;
    });

    std::unordered_set<uint64_t> seen;
    seen.reserve(hits.size());
    for (const auto &entry : hits) {
        uint64_t objectId = entry.second;
        if (objectId == 0)
            continue;
        if (seen.insert(objectId).second) {
            orderedIds.push_back(objectId);
        }
    }

    return orderedIds;
}

// ============================================================================
// Editor Tools — highlight + world ray for Python-side gizmo interaction
// ============================================================================

void InfEngine::SetEditorToolHighlight(int axis)
{
    if (!m_renderer) {
        return;
    }
    EditorTools *tools = m_renderer->GetEditorTools();
    if (!tools) {
        return;
    }

    EditorTools::HandleAxis ha = EditorTools::HandleAxis::None;
    switch (axis) {
    case 1:
        ha = EditorTools::HandleAxis::X;
        break;
    case 2:
        ha = EditorTools::HandleAxis::Y;
        break;
    case 3:
        ha = EditorTools::HandleAxis::Z;
        break;
    default:
        ha = EditorTools::HandleAxis::None;
        break;
    }
    tools->SetHighlightedAxis(ha);
}

void InfEngine::SetEditorToolMode(int mode)
{
    if (!m_renderer)
        return;
    EditorTools *tools = m_renderer->GetEditorTools();
    if (!tools)
        return;

    EditorTools::ToolMode tm = EditorTools::ToolMode::None;
    switch (mode) {
    case 1:
        tm = EditorTools::ToolMode::Translate;
        break;
    case 2:
        tm = EditorTools::ToolMode::Rotate;
        break;
    case 3:
        tm = EditorTools::ToolMode::Scale;
        break;
    default:
        tm = EditorTools::ToolMode::None;
        break;
    }
    tools->SetToolMode(tm);
}

int InfEngine::GetEditorToolMode() const
{
    if (!m_renderer)
        return 0;
    EditorTools *tools = m_renderer->GetEditorTools();
    if (!tools)
        return 0;

    switch (tools->GetToolMode()) {
    case EditorTools::ToolMode::Translate:
        return 1;
    case EditorTools::ToolMode::Rotate:
        return 2;
    case EditorTools::ToolMode::Scale:
        return 3;
    default:
        return 0;
    }
}

void InfEngine::SetEditorToolLocalMode(bool local)
{
    if (!m_renderer)
        return;
    EditorTools *tools = m_renderer->GetEditorTools();
    if (!tools)
        return;
    tools->SetLocalMode(local);
}

std::tuple<float, float, float, float, float, float>
InfEngine::ScreenToWorldRay(float screenX, float screenY, float viewportWidth, float viewportHeight)
{
    Camera *camera = SceneRenderBridge::Instance().GetActiveCamera();
    if (!camera || !camera->GetGameObject()) {
        return {0.f, 0.f, 0.f, 0.f, 0.f, -1.f};
    }

    auto [origin, dir] = camera->ScreenPointToRay(glm::vec2(screenX, screenY), viewportWidth, viewportHeight);

    return {origin.x, origin.y, origin.z, dir.x, dir.y, dir.z};
}

} // namespace infengine
