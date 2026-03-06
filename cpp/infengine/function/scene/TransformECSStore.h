#pragma once

#include "core/types/InfContiguousPool.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace infengine
{

class Scene;
class Transform;
class GameObject;

struct TransformECSData
{
    glm::vec3 localPosition{0.0f, 0.0f, 0.0f};
    glm::vec3 localEulerAngles{0.0f, 0.0f, 0.0f};
    glm::quat localRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 localScale{1.0f, 1.0f, 1.0f};
    bool dirty = true;
    glm::mat4 cachedWorldMatrix{1.0f};
    bool worldMatrixDirty = true;
    Transform *owner = nullptr;
};

class TransformECSStore
{
  public:
    using Handle = typename InfContiguousPool<TransformECSData>::Handle;

    static TransformECSStore &Instance();

    Handle Allocate(Transform *owner);
    void Release(Handle handle);
    [[nodiscard]] bool IsValid(Handle handle) const;

    TransformECSData &Get(Handle handle);
    const TransformECSData &Get(Handle handle) const;

    void RebindOwner(Handle handle, Transform *owner);

    void InvalidateSubtree(Transform *root) const;

    void SyncSceneWorldMatrices(Scene *scene);

  private:
    TransformECSStore() = default;

    void SyncObjectWorldMatrices(GameObject *obj);

    mutable InfContiguousPool<TransformECSData> m_pool;
};

} // namespace infengine
