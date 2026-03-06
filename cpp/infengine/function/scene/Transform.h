#pragma once

#define GLM_FORCE_RADIANS
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include "Component.h"
#include "TransformECSStore.h"
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace infengine
{

/**
 * @brief Transform component for position, rotation, and scale.
 *
 * Every GameObject has a Transform. It defines the object's position,
 * rotation (as Euler angles or quaternion), and scale in 3D space.
 *
 * Supports hierarchical transforms through parent-child relationships.
 */
class Transform : public Component
{
  public:
    Transform();
    ~Transform() override;

    [[nodiscard]] const char *GetTypeName() const override
    {
        return "Transform";
    }

    // ========================================================================
    // Local-space Position (direct access to stored local coordinates)
    // ========================================================================

    /// @brief Get position in local (parent) space
    [[nodiscard]] glm::vec3 GetLocalPosition() const
    {
        return TransformECSStore::Instance().Get(m_ecsHandle).localPosition;
    }

    /// @brief Set position in local (parent) space
    void SetLocalPosition(const glm::vec3 &position)
    {
        auto &data = TransformECSStore::Instance().Get(m_ecsHandle);
        data.localPosition = position;
        data.dirty = true;
        InvalidateWorldMatrix();
    }
    void SetLocalPosition(float x, float y, float z)
    {
        SetLocalPosition(glm::vec3(x, y, z));
    }

    // ========================================================================
    // World-space Position (considering parent hierarchy)
    // ========================================================================

    /// @brief Get position in world space (considering parent hierarchy)
    [[nodiscard]] glm::vec3 GetWorldPosition() const;

    /// @brief Set position in world space (computes required local position)
    void SetWorldPosition(const glm::vec3 &worldPos);
    void SetWorldPosition(float x, float y, float z)
    {
        SetWorldPosition(glm::vec3(x, y, z));
    }

    /// @brief Alias — matches Unity convention: GetPosition() == world space
    [[nodiscard]] glm::vec3 GetPosition() const
    {
        return GetWorldPosition();
    }
    void SetPosition(const glm::vec3 &pos)
    {
        SetWorldPosition(pos);
    }
    void SetPosition(float x, float y, float z)
    {
        SetWorldPosition(x, y, z);
    }

    // ========================================================================
    // Local-space Rotation
    // ========================================================================

    /// @brief Get rotation as Euler angles (degrees) in local space
    [[nodiscard]] glm::vec3 GetLocalEulerAngles() const
    {
        return TransformECSStore::Instance().Get(m_ecsHandle).localEulerAngles;
    }

    /// @brief Set rotation from Euler angles (degrees) in local space
    void SetLocalEulerAngles(const glm::vec3 &euler)
    {
        auto &data = TransformECSStore::Instance().Get(m_ecsHandle);
        data.localEulerAngles = euler;
        data.localRotation = glm::quat(glm::radians(euler));
        data.dirty = true;
        InvalidateWorldMatrix();
    }
    void SetLocalEulerAngles(float x, float y, float z)
    {
        SetLocalEulerAngles(glm::vec3(x, y, z));
    }

    /// @brief Get rotation as quaternion in local space
    [[nodiscard]] glm::quat GetLocalRotation() const
    {
        return TransformECSStore::Instance().Get(m_ecsHandle).localRotation;
    }

    /// @brief Set rotation from quaternion in local space
    void SetLocalRotation(const glm::quat &rotation)
    {
        auto &data = TransformECSStore::Instance().Get(m_ecsHandle);
        data.localRotation = rotation;
        data.localEulerAngles = ExtractEulerAngles(rotation);
        data.dirty = true;
        InvalidateWorldMatrix();
    }

    // ========================================================================
    // World-space Rotation (considering parent hierarchy)
    // ========================================================================

    /// @brief Get rotation as Euler angles (degrees) in world space
    [[nodiscard]] glm::vec3 GetWorldEulerAngles() const;

    /// @brief Set rotation from Euler angles (degrees) in world space
    void SetWorldEulerAngles(const glm::vec3 &euler);

    /// @brief Get world-space rotation quaternion (considering parent hierarchy)
    [[nodiscard]] glm::quat GetWorldRotation() const;

    /// @brief Set rotation from quaternion in world space
    void SetWorldRotation(const glm::quat &worldRot);

    /// @brief Aliases — Unity convention: Get/SetEulerAngles() == world space
    [[nodiscard]] glm::vec3 GetEulerAngles() const
    {
        return GetWorldEulerAngles();
    }
    void SetEulerAngles(const glm::vec3 &euler)
    {
        SetWorldEulerAngles(euler);
    }
    void SetEulerAngles(float x, float y, float z)
    {
        SetWorldEulerAngles(glm::vec3(x, y, z));
    }
    [[nodiscard]] glm::quat GetRotation() const
    {
        return GetWorldRotation();
    }
    void SetRotation(const glm::quat &q)
    {
        SetWorldRotation(q);
    }

    // ========================================================================
    // Local-space Scale
    // ========================================================================

    [[nodiscard]] glm::vec3 GetLocalScale() const
    {
        return TransformECSStore::Instance().Get(m_ecsHandle).localScale;
    }
    void SetLocalScale(const glm::vec3 &scale)
    {
        auto &data = TransformECSStore::Instance().Get(m_ecsHandle);
        data.localScale = scale;
        data.dirty = true;
        InvalidateWorldMatrix();
    }
    void SetLocalScale(float x, float y, float z)
    {
        SetLocalScale(glm::vec3(x, y, z));
    }
    void SetLocalScale(float uniform)
    {
        SetLocalScale(glm::vec3(uniform));
    }

    /// @brief Aliases — GetScale/SetScale map to local scale (Unity convention)
    [[nodiscard]] glm::vec3 GetScale() const
    {
        return TransformECSStore::Instance().Get(m_ecsHandle).localScale;
    }
    void SetScale(const glm::vec3 &s)
    {
        SetLocalScale(s);
    }
    void SetScale(float x, float y, float z)
    {
        SetLocalScale(x, y, z);
    }
    void SetScale(float u)
    {
        SetLocalScale(u);
    }

    // ========================================================================
    // World-space Scale (approximate, read-only lossyScale + writable setter)
    // ========================================================================

    /// @brief Get approximate world scale (product of parent scales, like Unity lossyScale)
    [[nodiscard]] glm::vec3 GetWorldScale() const;

    /// @brief Set world scale (computes required local scale given parent hierarchy)
    void SetWorldScale(const glm::vec3 &worldScale);

    // ========================================================================
    // Hierarchy (delegates to owning GameObject, Unity-style access)
    // ========================================================================

    /// @brief Get parent Transform (nullptr if root). Unity: transform.parent
    [[nodiscard]] Transform *GetParent() const;

    /// @brief Set parent Transform. Unity: transform.SetParent(parent, worldPositionStays)
    void SetParent(Transform *parent, bool worldPositionStays = true);

    /// @brief Get the topmost Transform in the hierarchy. Unity: transform.root
    [[nodiscard]] Transform *GetRoot();

    /// @brief Number of children. Unity: transform.childCount
    [[nodiscard]] size_t GetChildCount() const;

    /// @brief Get child Transform by index. Unity: transform.GetChild(index)
    [[nodiscard]] Transform *GetChild(size_t index) const;

    /// @brief Find a child by name (non-recursive). Unity: transform.Find(name)
    [[nodiscard]] Transform *Find(const std::string &name) const;

    /// @brief Unparent all children. Unity: transform.DetachChildren()
    void DetachChildren();

    /// @brief Is this transform a child of parent? Unity: transform.IsChildOf(parent)
    [[nodiscard]] bool IsChildOf(const Transform *parent) const;

    /// @brief Get sibling index in parent's children list. Unity: transform.GetSiblingIndex()
    [[nodiscard]] int GetSiblingIndex() const;

    /// @brief Set sibling index. Unity: transform.SetSiblingIndex(index)
    void SetSiblingIndex(int index);

    /// @brief Move to first sibling position. Unity: transform.SetAsFirstSibling()
    void SetAsFirstSibling();

    /// @brief Move to last sibling position. Unity: transform.SetAsLastSibling()
    void SetAsLastSibling();

    // ========================================================================
    // Direction vectors
    // ========================================================================

    /// @brief Get forward direction in world space (Unity convention: transform.forward)
    [[nodiscard]] glm::vec3 GetForward() const
    {
        return GetWorldForward();
    }

    /// @brief Get right direction in world space (Unity convention: transform.right)
    [[nodiscard]] glm::vec3 GetRight() const
    {
        return GetWorldRight();
    }

    /// @brief Get up direction in world space (Unity convention: transform.up)
    [[nodiscard]] glm::vec3 GetUp() const
    {
        return GetWorldUp();
    }

    // ========================================================================
    // Local-space direction vectors
    // ========================================================================

    /// @brief Get forward direction in local space (negative Z, local rotation only)
    [[nodiscard]] glm::vec3 GetLocalForward() const
    {
        return glm::normalize(GetLocalRotation() * glm::vec3(0.0f, 0.0f, -1.0f));
    }

    /// @brief Get right direction in local space (positive X, local rotation only)
    [[nodiscard]] glm::vec3 GetLocalRight() const
    {
        return glm::normalize(GetLocalRotation() * glm::vec3(1.0f, 0.0f, 0.0f));
    }

    /// @brief Get up direction in local space (positive Y, local rotation only)
    [[nodiscard]] glm::vec3 GetLocalUp() const
    {
        return glm::normalize(GetLocalRotation() * glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // ========================================================================
    // World-space direction vectors (considering parent hierarchy)
    // ========================================================================

    /// @brief Get forward direction in world space (negative Z, considering parents)
    [[nodiscard]] glm::vec3 GetWorldForward() const;

    /// @brief Get right direction in world space (positive X, considering parents)
    [[nodiscard]] glm::vec3 GetWorldRight() const;

    /// @brief Get up direction in world space (positive Y, considering parents)
    [[nodiscard]] glm::vec3 GetWorldUp() const;

    // ========================================================================
    // Transformations
    // ========================================================================

    /// @brief Translate by delta in world space (Unity convention)
    void Translate(const glm::vec3 &delta);

    /// @brief Translate by delta in local space (along object's own axes)
    void TranslateLocal(const glm::vec3 &delta);

    /// @brief Rotate by Euler angles (degrees) in local space. Unity: transform.Rotate(euler)
    void Rotate(const glm::vec3 &euler)
    {
        auto &data = TransformECSStore::Instance().Get(m_ecsHandle);
        glm::quat deltaRotation = glm::quat(glm::radians(euler));
        data.localRotation = deltaRotation * data.localRotation;
        data.localEulerAngles = ExtractEulerAngles(data.localRotation);
        data.dirty = true;
        InvalidateWorldMatrix();
    }

    /// @brief Rotate around a local axis by angle (degrees). Unity: transform.Rotate(axis, angle)
    void Rotate(const glm::vec3 &axis, float angle)
    {
        auto &data = TransformECSStore::Instance().Get(m_ecsHandle);
        glm::quat deltaRotation = glm::angleAxis(glm::radians(angle), glm::normalize(axis));
        data.localRotation = deltaRotation * data.localRotation;
        data.localEulerAngles = ExtractEulerAngles(data.localRotation);
        data.dirty = true;
        InvalidateWorldMatrix();
    }

    /// @brief Rotate around a world-space point. Unity: transform.RotateAround(point, axis, angle)
    void RotateAround(const glm::vec3 &point, const glm::vec3 &axis, float angle);

    /// @brief Look at a world-space target position (Unity convention)
    void LookAt(const glm::vec3 &target, const glm::vec3 &up = glm::vec3(0.0f, 1.0f, 0.0f));

    // ========================================================================
    // Space conversion methods (Unity: TransformPoint, InverseTransformPoint, etc.)
    // ========================================================================

    /// @brief Transform position from local space to world space. Unity: transform.TransformPoint(point)
    [[nodiscard]] glm::vec3 TransformPoint(const glm::vec3 &point) const;

    /// @brief Transform position from world space to local space. Unity: transform.InverseTransformPoint(point)
    [[nodiscard]] glm::vec3 InverseTransformPoint(const glm::vec3 &point) const;

    /// @brief Transform direction from local to world (rotation only, no scale). Unity:
    /// transform.TransformDirection(dir)
    [[nodiscard]] glm::vec3 TransformDirection(const glm::vec3 &direction) const;

    /// @brief Transform direction from world to local (rotation only, no scale). Unity:
    /// transform.InverseTransformDirection(dir)
    [[nodiscard]] glm::vec3 InverseTransformDirection(const glm::vec3 &direction) const;

    /// @brief Transform vector from local to world (with scale, no position). Unity: transform.TransformVector(vec)
    [[nodiscard]] glm::vec3 TransformVector(const glm::vec3 &vector) const;

    /// @brief Transform vector from world to local (with scale, no position). Unity:
    /// transform.InverseTransformVector(vec)
    [[nodiscard]] glm::vec3 InverseTransformVector(const glm::vec3 &vector) const;

    // ========================================================================
    // Matrix
    // ========================================================================

    /// @brief Get local transformation matrix (from local position/rotation/scale)
    [[nodiscard]] glm::mat4 GetLocalMatrix() const
    {
        const auto &data = TransformECSStore::Instance().Get(m_ecsHandle);
        glm::mat4 translation = glm::translate(glm::mat4(1.0f), data.localPosition);
        glm::mat4 rotation = glm::mat4_cast(data.localRotation);
        glm::mat4 scale = glm::scale(glm::mat4(1.0f), data.localScale);
        return translation * rotation * scale;
    }

    /// @brief Get world transformation matrix (considering parent hierarchy). Unity: transform.localToWorldMatrix
    [[nodiscard]] glm::mat4 GetWorldMatrix() const;

    /// @brief Alias for GetWorldMatrix(). Unity naming: localToWorldMatrix
    [[nodiscard]] glm::mat4 GetLocalToWorldMatrix() const
    {
        return GetWorldMatrix();
    }

    /// @brief Get inverse world matrix (world to local). Unity: transform.worldToLocalMatrix
    [[nodiscard]] glm::mat4 GetWorldToLocalMatrix() const;

    /// @brief Check if transform has been modified. Unity: transform.hasChanged
    [[nodiscard]] bool HasChanged() const
    {
        return TransformECSStore::Instance().Get(m_ecsHandle).dirty;
    }

    /// @brief Legacy alias for HasChanged()
    [[nodiscard]] bool IsDirty() const
    {
        return TransformECSStore::Instance().Get(m_ecsHandle).dirty;
    }

    /// @brief Clear changed flag. Unity: transform.hasChanged = false
    void SetHasChanged(bool value)
    {
        TransformECSStore::Instance().Get(m_ecsHandle).dirty = value;
    }

    /// @brief Legacy alias for SetHasChanged(false)
    void ClearDirty()
    {
        TransformECSStore::Instance().Get(m_ecsHandle).dirty = false;
    }

    [[nodiscard]] TransformECSStore::Handle GetECSHandle() const
    {
        return m_ecsHandle;
    }

    /// @brief Recursively invalidate cached world matrix for this transform and all children.
    void InvalidateWorldMatrix() const;

    // ========================================================================
    // Serialization
    // ========================================================================

    [[nodiscard]] std::string Serialize() const override;
    bool Deserialize(const std::string &jsonStr) override;

  private:
    /// @brief Extract Euler angles from quaternion in a consistent way.
    /// This ensures that SetEulerAngles -> GetEulerAngles round-trips correctly.
    /// GLM's glm::quat(vec3) uses pitch-yaw-roll order applied as ZYX intrinsic.
    static glm::vec3 ExtractEulerAngles(const glm::quat &q)
    {
        // Use GLM's built-in function for consistency
        // glm::eulerAngles returns angles in XYZ order (pitch, yaw, roll)
        return glm::degrees(glm::eulerAngles(q));
    }

    TransformECSStore::Handle m_ecsHandle;
};

} // namespace infengine
