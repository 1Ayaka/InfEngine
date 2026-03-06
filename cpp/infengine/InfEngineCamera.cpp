/**
 * @file InfEngineCamera.cpp
 * @brief InfEngine — Editor camera control methods
 *
 * Split from InfEngine.cpp for maintainability.
 * Contains: ProcessSceneViewInput, Get/SetEditorCamera*, ResetEditorCamera,
 *           FocusEditorCameraOn.
 */

#include "InfEngine.h"

#include <glm/glm.hpp>

namespace infengine
{

// Static input state tracking (simple solution without modifying header extensively)
static bool s_lastRightMouseDown = false;
static bool s_lastMiddleMouseDown = false;
static float s_lastMouseX = 0.0f;
static float s_lastMouseY = 0.0f;

// ----------------------------------
// Editor Camera Access
// ----------------------------------

EditorCameraController *InfEngine::GetEditorCamera()
{
    if (m_isCleanedUp) {
        return nullptr;
    }
    return &SceneManager::Instance().GetEditorCameraController();
}

// ----------------------------------
// Scene Camera Control
// ----------------------------------

void InfEngine::ProcessSceneViewInput(float deltaTime, bool rightMouseDown, bool middleMouseDown, float mouseDeltaX,
                                      float mouseDeltaY, float scrollDelta, bool keyW, bool keyA, bool keyS, bool keyD,
                                      bool keyQ, bool keyE, bool keyShift)
{
    if (m_isCleanedUp) {
        return;
    }

    EditorCameraController &controller = SceneManager::Instance().GetEditorCameraController();

    // Handle mouse button state changes
    if (rightMouseDown) {
        if (!s_lastRightMouseDown) {
            controller.OnMouseButtonDown(1, 0, 0);
        }
    } else {
        if (s_lastRightMouseDown) {
            controller.OnMouseButtonUp(1, 0, 0);
        }
    }
    s_lastRightMouseDown = rightMouseDown;

    if (middleMouseDown) {
        if (!s_lastMiddleMouseDown) {
            controller.OnMouseButtonDown(2, 0, 0);
        }
    } else {
        if (s_lastMiddleMouseDown) {
            controller.OnMouseButtonUp(2, 0, 0);
        }
    }
    s_lastMiddleMouseDown = middleMouseDown;

    // Apply mouse movement - Python side already handles priority
    // Only apply if there's actual delta
    if (mouseDeltaX != 0.0f || mouseDeltaY != 0.0f) {
        if (rightMouseDown && !middleMouseDown) {
            // Rotate mode
            controller.ApplyRotation(mouseDeltaX, mouseDeltaY);
        } else if (middleMouseDown && !rightMouseDown) {
            // Pan mode
            controller.ApplyPan(mouseDeltaX, mouseDeltaY);
        } else if (rightMouseDown && middleMouseDown) {
            // Both pressed: prioritize rotation
            controller.ApplyRotation(mouseDeltaX, mouseDeltaY);
        }
    }

    // Handle scroll
    if (scrollDelta != 0) {
        controller.OnMouseScroll(scrollDelta);
    }

    // Debug: Log key states when right mouse is down
    // if (rightMouseDown && (keyW || keyA || keyS || keyD || keyQ || keyE)) {
    //     INFLOG_INFO("Keys: W=", keyW, " A=", keyA, " S=", keyS, " D=", keyD, " Q=", keyQ, " E=", keyE);
    // }

    // Handle key state
    if (keyW)
        controller.OnKeyDown(87);
    else
        controller.OnKeyUp(87);
    if (keyA)
        controller.OnKeyDown(65);
    else
        controller.OnKeyUp(65);
    if (keyS)
        controller.OnKeyDown(83);
    else
        controller.OnKeyUp(83);
    if (keyD)
        controller.OnKeyDown(68);
    else
        controller.OnKeyUp(68);
    if (keyQ)
        controller.OnKeyDown(81);
    else
        controller.OnKeyUp(81);
    if (keyE)
        controller.OnKeyDown(69);
    else
        controller.OnKeyUp(69);
    if (keyShift)
        controller.OnKeyDown(340);
    else
        controller.OnKeyUp(340);

    // Update camera controller (for fly mode)
    controller.Update(deltaTime);
}

void InfEngine::GetEditorCameraPosition(float *outX, float *outY, float *outZ)
{
    if (m_isCleanedUp) {
        if (outX)
            *outX = 0;
        if (outY)
            *outY = 0;
        if (outZ)
            *outZ = 0;
        return;
    }

    EditorCameraController &controller = SceneManager::Instance().GetEditorCameraController();
    Camera *cam = controller.GetCamera();
    if (cam && cam->GetGameObject()) {
        glm::vec3 pos = cam->GetGameObject()->GetTransform()->GetPosition();
        if (outX)
            *outX = pos.x;
        if (outY)
            *outY = pos.y;
        if (outZ)
            *outZ = pos.z;
    }
}

void InfEngine::GetEditorCameraRotation(float *outYaw, float *outPitch)
{
    if (m_isCleanedUp) {
        if (outYaw)
            *outYaw = 0;
        if (outPitch)
            *outPitch = 0;
        return;
    }

    EditorCameraController &controller = SceneManager::Instance().GetEditorCameraController();
    if (outYaw)
        *outYaw = controller.GetYaw();
    if (outPitch)
        *outPitch = controller.GetPitch();
}

float InfEngine::GetEditorCameraFov()
{
    if (m_isCleanedUp) {
        return 60.0f;
    }
    Camera *camera = SceneManager::Instance().GetEditorCameraController().GetCamera();
    if (camera) {
        return camera->GetFieldOfView();
    }
    return 60.0f;
}

void InfEngine::SetEditorCameraFov(float fov)
{
    if (m_isCleanedUp) {
        return;
    }
    Camera *camera = SceneManager::Instance().GetEditorCameraController().GetCamera();
    if (camera) {
        camera->SetFieldOfView(fov);
    }
}

float InfEngine::GetEditorCameraNearClip()
{
    if (m_isCleanedUp) {
        return 0.01f;
    }
    Camera *camera = SceneManager::Instance().GetEditorCameraController().GetCamera();
    if (camera) {
        return camera->GetNearClip();
    }
    return 0.01f;
}

void InfEngine::SetEditorCameraNearClip(float nearClip)
{
    if (m_isCleanedUp) {
        return;
    }
    Camera *camera = SceneManager::Instance().GetEditorCameraController().GetCamera();
    if (camera) {
        camera->SetNearClip(nearClip);
    }
}

float InfEngine::GetEditorCameraFarClip()
{
    if (m_isCleanedUp) {
        return 1000.0f;
    }
    Camera *camera = SceneManager::Instance().GetEditorCameraController().GetCamera();
    if (camera) {
        return camera->GetFarClip();
    }
    return 1000.0f;
}

void InfEngine::SetEditorCameraFarClip(float farClip)
{
    if (m_isCleanedUp) {
        return;
    }
    Camera *camera = SceneManager::Instance().GetEditorCameraController().GetCamera();
    if (camera) {
        camera->SetFarClip(farClip);
    }
}

void InfEngine::ResetEditorCamera()
{
    if (m_isCleanedUp) {
        return;
    }

    EditorCameraController &controller = SceneManager::Instance().GetEditorCameraController();
    controller.Reset();
}

void InfEngine::FocusEditorCameraOn(float x, float y, float z, float distance)
{
    if (m_isCleanedUp) {
        return;
    }

    EditorCameraController &controller = SceneManager::Instance().GetEditorCameraController();
    controller.FocusOn(glm::vec3(x, y, z), distance);
}

void InfEngine::GetEditorCameraFocusPoint(float *outX, float *outY, float *outZ)
{
    if (m_isCleanedUp) {
        if (outX)
            *outX = 0;
        if (outY)
            *outY = 0;
        if (outZ)
            *outZ = 0;
        return;
    }

    EditorCameraController &controller = SceneManager::Instance().GetEditorCameraController();
    glm::vec3 fp = controller.GetFocusPoint();
    if (outX)
        *outX = fp.x;
    if (outY)
        *outY = fp.y;
    if (outZ)
        *outZ = fp.z;
}

float InfEngine::GetEditorCameraFocusDistance()
{
    if (m_isCleanedUp) {
        return 10.0f;
    }
    return SceneManager::Instance().GetEditorCameraController().GetFocusDistance();
}

void InfEngine::RestoreEditorCameraState(float posX, float posY, float posZ, float focusX, float focusY, float focusZ,
                                         float focusDist, float yaw, float pitch)
{
    if (m_isCleanedUp) {
        return;
    }
    EditorCameraController &controller = SceneManager::Instance().GetEditorCameraController();
    controller.RestoreState(glm::vec3(posX, posY, posZ), glm::vec3(focusX, focusY, focusZ), focusDist, yaw, pitch);
}

} // namespace infengine
