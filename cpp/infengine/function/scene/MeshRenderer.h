#pragma once

#include "Component.h"
#include "function/renderer/InfRenderStruct.h"
#include <cstdint>
#include <function/resources/AssetRef.h>
#include <function/resources/InfMaterial/InfMaterial.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace infengine
{

/**
 * @brief Reference to a mesh resource for rendering.
 *
 * This is a lightweight reference to mesh data stored elsewhere.
 * The actual vertex/index data is managed by the resource system.
 */
struct MeshRef
{
    uint64_t meshId = 0;  // Resource ID for the mesh
    std::string meshPath; // Path to the mesh file (for debugging/reloading)

    bool IsValid() const
    {
        return meshId != 0;
    }
};

/**
 * @brief Reference to a material for rendering.
 */
struct MaterialRef
{
    uint64_t materialId = 0;  // Resource ID for the material
    std::string materialPath; // Path to the material (for debugging/reloading)

    bool IsValid() const
    {
        return materialId != 0;
    }
};

/**
 * @brief MeshRenderer component for rendering 3D meshes.
 *
 * Attach to a GameObject to make it render a mesh.
 * The renderer uses the Transform of the GameObject for positioning.
 */
class MeshRenderer : public Component
{
  public:
    MeshRenderer() = default;
    ~MeshRenderer() override;

    [[nodiscard]] const char *GetTypeName() const override
    {
        return "MeshRenderer";
    }

    // ========================================================================
    // Lifecycle — register/unregister with SceneManager component registry
    // ========================================================================

    void OnEnable() override;
    void OnDisable() override;

    // ========================================================================
    // Mesh
    // ========================================================================

    [[nodiscard]] const MeshRef &GetMesh() const
    {
        return m_mesh;
    }
    void SetMesh(const MeshRef &mesh)
    {
        m_mesh = mesh;
    }
    void SetMesh(uint64_t meshId, const std::string &path = "")
    {
        m_mesh.meshId = meshId;
        m_mesh.meshPath = path;
        m_useInlineMesh = false;
    }

    /// @brief Set mesh from inline vertex/index data (for primitives)
    void SetMesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices)
    {
        m_inlineVertices = std::move(vertices);
        m_inlineIndices = std::move(indices);
        m_useInlineMesh = true;
        ComputeLocalBoundsFromInlineVertices();
    }

    /// @brief Check if this renderer uses inline mesh data
    [[nodiscard]] bool HasInlineMesh() const
    {
        return m_useInlineMesh;
    }

    /// @brief Get inline vertex data
    [[nodiscard]] const std::vector<Vertex> &GetInlineVertices() const
    {
        return m_inlineVertices;
    }

    /// @brief Get inline index data
    [[nodiscard]] const std::vector<uint32_t> &GetInlineIndices() const
    {
        return m_inlineIndices;
    }

    // ========================================================================
    // Materials
    // ========================================================================

    [[nodiscard]] const std::vector<MaterialRef> &GetMaterials() const
    {
        return m_materials;
    }
    void SetMaterials(const std::vector<MaterialRef> &materials)
    {
        m_materials = materials;
    }

    void SetMaterial(size_t index, const MaterialRef &material)
    {
        if (index >= m_materials.size()) {
            m_materials.resize(index + 1);
        }
        m_materials[index] = material;
    }

    void AddMaterial(const MaterialRef &material)
    {
        m_materials.push_back(material);
    }

    [[nodiscard]] size_t GetMaterialCount() const
    {
        return m_materials.size();
    }

    // ========================================================================
    // Material (InfMaterial-based, for actual rendering)
    // ========================================================================

    /// @brief Get the primary material for rendering
    [[nodiscard]] std::shared_ptr<InfMaterial> GetRenderMaterial() const
    {
        return m_renderMaterial;
    }

    /// @brief Set the primary render material
    /// @param material The InfMaterial to use (nullptr will use default material)
    void SetRenderMaterial(std::shared_ptr<InfMaterial> material)
    {
        m_renderMaterial = material;
    }

    /// @brief Check if this renderer has a material assigned
    [[nodiscard]] bool HasRenderMaterial() const
    {
        return m_renderMaterial != nullptr;
    }

    // ---- GUID-based material reference (AssetRef) ----

    /// @brief Get the material asset reference (GUID-based)
    [[nodiscard]] const AssetRef<InfMaterial> &GetMaterialAssetRef() const
    {
        return m_materialAssetRef;
    }

    /// @brief Set the material asset reference by GUID
    void SetMaterialAssetRef(const std::string &guid)
    {
        m_materialAssetRef.SetGuid(guid);
    }

    /// @brief Set the material asset reference with a resolved pointer
    void SetMaterialAssetRef(const std::string &guid, std::shared_ptr<InfMaterial> material)
    {
        m_materialAssetRef = AssetRef<InfMaterial>(guid, std::move(material));
        m_renderMaterial = m_materialAssetRef.Get(); // keep legacy field in sync
    }

    /// @brief Get the effective material (returns default if none assigned)
    [[nodiscard]] std::shared_ptr<InfMaterial> GetEffectiveMaterial() const;

    // ========================================================================
    // Rendering flags
    // ========================================================================

    [[nodiscard]] bool CastsShadows() const
    {
        return m_castShadows;
    }
    void SetCastShadows(bool cast)
    {
        m_castShadows = cast;
    }

    [[nodiscard]] bool ReceivesShadows() const
    {
        return m_receiveShadows;
    }
    void SetReceivesShadows(bool receive)
    {
        m_receiveShadows = receive;
    }

    // ========================================================================
    // Bounds (for culling)
    // ========================================================================

    /// @brief Get local-space bounding box (from mesh)
    [[nodiscard]] const glm::vec3 &GetLocalBoundsMin() const
    {
        return m_localBoundsMin;
    }
    [[nodiscard]] const glm::vec3 &GetLocalBoundsMax() const
    {
        return m_localBoundsMax;
    }

    /// @brief Set local bounds (usually from mesh loading)
    void SetLocalBounds(const glm::vec3 &min, const glm::vec3 &max)
    {
        m_localBoundsMin = min;
        m_localBoundsMax = max;
    }

    /// @brief Get world-space bounding box (transformed by GameObject)
    [[nodiscard]] void GetWorldBounds(glm::vec3 &outMin, glm::vec3 &outMax) const;

    /// @brief Recompute local bounds from inline vertex positions.
    void ComputeLocalBoundsFromInlineVertices();

    // ========================================================================
    // Serialization
    // ========================================================================

    [[nodiscard]] std::string Serialize() const override;
    bool Deserialize(const std::string &jsonStr) override;

  private:
    MeshRef m_mesh;
    std::vector<MaterialRef> m_materials;

    // Actual render material (InfMaterial)
    std::shared_ptr<InfMaterial> m_renderMaterial;

    // GUID-based material reference (new — coexists with legacy m_renderMaterial during migration)
    AssetRef<InfMaterial> m_materialAssetRef;

    // Inline mesh data (for primitives, not using resource system)
    std::vector<Vertex> m_inlineVertices;
    std::vector<uint32_t> m_inlineIndices;
    bool m_useInlineMesh = false;

    bool m_castShadows = true;
    bool m_receiveShadows = true;

    // Local-space bounding box
    glm::vec3 m_localBoundsMin{-0.5f};
    glm::vec3 m_localBoundsMax{0.5f};
};

} // namespace infengine
