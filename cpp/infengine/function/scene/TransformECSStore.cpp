#include "TransformECSStore.h"
#include "GameObject.h"
#include "Scene.h"
#include "Transform.h"
#include <glm/gtc/matrix_transform.hpp>

namespace infengine
{

TransformECSStore &TransformECSStore::Instance()
{
    static TransformECSStore instance;
    return instance;
}

TransformECSStore::Handle TransformECSStore::Allocate(Transform *owner)
{
    Handle handle = m_pool.Allocate();
    TransformECSData &data = m_pool.Get(handle);
    data = TransformECSData{};
    data.owner = owner;
    return handle;
}

void TransformECSStore::Release(Handle handle)
{
    if (!m_pool.IsAlive(handle)) {
        return;
    }
    m_pool.Get(handle).owner = nullptr;
    m_pool.Free(handle);
}

bool TransformECSStore::IsValid(Handle handle) const
{
    return m_pool.IsAlive(handle);
}

TransformECSData &TransformECSStore::Get(Handle handle)
{
    return m_pool.Get(handle);
}

const TransformECSData &TransformECSStore::Get(Handle handle) const
{
    return m_pool.Get(handle);
}

void TransformECSStore::RebindOwner(Handle handle, Transform *owner)
{
    if (!m_pool.IsAlive(handle)) {
        return;
    }
    m_pool.Get(handle).owner = owner;
}

void TransformECSStore::InvalidateSubtree(Transform *root) const
{
    if (!root) {
        return;
    }

    auto handle = root->GetECSHandle();
    if (!m_pool.IsAlive(handle)) {
        return;
    }

    TransformECSData &data = const_cast<InfContiguousPool<TransformECSData> &>(m_pool).Get(handle);
    if (!data.worldMatrixDirty) {
        data.worldMatrixDirty = true;
    }

    GameObject *go = root->GetGameObject();
    if (!go) {
        return;
    }

    for (size_t i = 0; i < go->GetChildCount(); ++i) {
        GameObject *child = go->GetChild(i);
        if (child) {
            InvalidateSubtree(child->GetTransform());
        }
    }
}

void TransformECSStore::SyncSceneWorldMatrices(Scene *scene)
{
    if (!scene) {
        return;
    }

    const auto &roots = scene->GetRootObjects();
    for (const auto &root : roots) {
        SyncObjectWorldMatrices(root.get());
    }
}

void TransformECSStore::SyncObjectWorldMatrices(GameObject *obj)
{
    if (!obj) {
        return;
    }

    Transform *t = obj->GetTransform();
    if (t) {
        auto handle = t->GetECSHandle();
        if (m_pool.IsAlive(handle)) {
            TransformECSData &data = m_pool.Get(handle);
            if (data.worldMatrixDirty) {
                glm::mat4 local = glm::translate(glm::mat4(1.0f), data.localPosition) *
                                  glm::mat4_cast(data.localRotation) * glm::scale(glm::mat4(1.0f), data.localScale);

                GameObject *parent = obj->GetParent();
                if (!parent) {
                    data.cachedWorldMatrix = local;
                } else {
                    Transform *pt = parent->GetTransform();
                    auto parentHandle = pt->GetECSHandle();
                    if (m_pool.IsAlive(parentHandle)) {
                        const TransformECSData &pd = m_pool.Get(parentHandle);
                        data.cachedWorldMatrix = pd.cachedWorldMatrix * local;
                    } else {
                        data.cachedWorldMatrix = local;
                    }
                }
                data.worldMatrixDirty = false;
            }
        }
    }

    for (size_t i = 0; i < obj->GetChildCount(); ++i) {
        SyncObjectWorldMatrices(obj->GetChild(i));
    }
}

} // namespace infengine
