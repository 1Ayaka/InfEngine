#pragma once

#include <core/types/InfFwdType.h>
#include <function/resources/AssetDependencyGraph.h>
#include <function/resources/AssetImporter/ImporterRegistry.h>
#include <function/resources/AssetRegistry/IAssetLoader.h>
#include <function/resources/InfResource/InfResourceMeta.h>

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace infengine
{

class InfRenderer;

/**
 * @brief Central asset database for the project.
 *
 * Responsibilities:
 * - Import assets and generate .meta files
 * - Maintain GUID <-> path mappings
 * - Provide Asset CRUD operations for editor and file watcher
 * - Own the ImporterRegistry and drive dependency scanning on import
 * - Dispatch AssetEvent notifications via AssetDependencyGraph
 * - Manage resource loaders, metadata cache and compiled resource cache
 */
class AssetDatabase
{
  public:
    AssetDatabase();

    /// @brief Initialize database with project root path.
    /// Also creates and registers all built-in importers.
    void Initialize(const std::string &projectRoot);

    /// @brief Refresh all assets by scanning the Assets folder
    void Refresh();

    /// @brief Add an extra directory to scan during Refresh (e.g. Library/Resources).
    void AddScanRoot(const std::string &path);

    /// @brief Import an asset and create/update its meta.
    /// Runs the appropriate AssetImporter to scan dependencies.
    /// @return GUID of the asset, empty if failed
    std::string ImportAsset(const std::string &path);

    /// @brief Delete asset and meta.
    /// Notifies dependents via AssetDependencyGraph::NotifyEvent(Deleted).
    bool DeleteAsset(const std::string &path);

    /// @brief Move/rename asset preserving GUID.
    /// Notifies dependents via AssetDependencyGraph::NotifyEvent(Moved).
    bool MoveAsset(const std::string &oldPath, const std::string &newPath);

    /// @brief Check if a GUID exists
    [[nodiscard]] bool ContainsGuid(const std::string &guid) const;

    /// @brief Check if a path exists in database
    [[nodiscard]] bool ContainsPath(const std::string &path) const;

    /// @brief Get GUID by path (empty if not found)
    [[nodiscard]] std::string GetGuidFromPath(const std::string &path) const;

    /// @brief Get path by GUID (empty if not found)
    [[nodiscard]] std::string GetPathFromGuid(const std::string &guid) const;

    /// @brief Get meta by GUID
    [[nodiscard]] const InfResourceMeta *GetMetaByGuid(const std::string &guid) const;

    /// @brief Get meta by path
    [[nodiscard]] const InfResourceMeta *GetMetaByPath(const std::string &path) const;

    /// @brief Get all GUIDs
    [[nodiscard]] std::vector<std::string> GetAllGuids() const;

    /// @brief Check if path is within Assets folder
    [[nodiscard]] bool IsAssetPath(const std::string &path) const;

    /// @brief Get project root
    [[nodiscard]] const std::string &GetProjectRoot() const
    {
        return m_projectRoot;
    }

    /// @brief Get assets root
    [[nodiscard]] const std::string &GetAssetsRoot() const
    {
        return m_assetsRoot;
    }

    /// @brief Access the dependency graph (singleton shorthand)
    [[nodiscard]] static AssetDependencyGraph &GetDependencyGraph()
    {
        return AssetDependencyGraph::Instance();
    }

    /// @brief Access the importer registry
    [[nodiscard]] ImporterRegistry &GetImporterRegistry()
    {
        return m_importerRegistry;
    }

    // ========================================================================
    // File watcher hooks
    // ========================================================================

    void OnAssetCreated(const std::string &path);
    void OnAssetModified(const std::string &path);
    void OnAssetDeleted(const std::string &path);
    void OnAssetMoved(const std::string &oldPath, const std::string &newPath);

    // ========================================================================
    // Resource management
    // ========================================================================

    /// @brief Remove resource meta by UID
    void RemoveResourceMeta(const std::string &uid);

    /// @brief Unified file reading method with automatic binary detection.
    bool ReadFile(const std::string &filePath, std::vector<char> &content) const;

    /// @brief Register a resource file and create/load its .meta.
    /// @return the GUID of the registered resource.
    std::string RegisterResource(const std::string &filePath, ResourceType type);

    /// @brief Notify that a resource has been modified, update meta
    void ModifyResource(const std::string &filePath);

    /// @brief Delete a resource from caches and its meta file
    void DeleteResource(const std::string &filePath);

    /// @brief Move/rename a resource file, preserving its GUID
    void MoveResource(const std::string &oldFilePath, const std::string &newFilePath);

    /// @brief Get all registered resource GUIDs (from meta cache)
    [[nodiscard]] std::vector<std::string> GetAllResourceGuids() const;

    /// @brief Find shader file path by shader_id
    [[nodiscard]] std::string FindShaderPathById(const std::string &shaderId, const std::string &shaderType) const;

    /// @brief Get resource type by file extension
    [[nodiscard]] ResourceType GetResourcesType(const std::string &extensionName) const;

    /// @brief Get resource type from a file path
    [[nodiscard]] ResourceType GetResourceTypeForPath(const std::string &filePath) const;

  private:
    [[nodiscard]] std::string NormalizePath(const std::string &path) const;
    [[nodiscard]] bool IsMetaFile(const std::filesystem::path &path) const;
    void UpdateMapping(const std::string &guid, const std::string &path);
    void RemoveMappingByGuid(const std::string &guid);
    void RemoveMappingByPath(const std::string &path);

    /// Run the matching importer for this asset (dependency scanning etc.)
    void RunImporter(const std::string &guid, const std::string &path, bool isReimport);

    /// @brief Determine if a file is binary based on extension and content
    [[nodiscard]] bool IsBinaryFile(const std::string &filePath) const;

    /// @brief Detect binary file by examining file content
    [[nodiscard]] bool DetectBinaryByContent(const std::string &filePath) const;

    std::string m_projectRoot;
    std::string m_assetsRoot;
    std::vector<std::string> m_extraScanRoots;

    // GUID -> path
    std::unordered_map<std::string, std::string> m_guidToPath;
    // normalized path -> GUID
    std::unordered_map<std::string, std::string> m_pathToGuid;

    // Asset importer registry (populated in Initialize)
    ImporterRegistry m_importerRegistry;

    // Resource loaders (one per ResourceType, used for meta creation/loading)
    // Non-owning pointers — ownership is in AssetRegistry::m_loaders.
    std::unordered_map<ResourceType, IAssetLoader *> m_loaders;
    // GUID -> metadata cache
    std::unordered_map<std::string, std::unique_ptr<InfResourceMeta>> m_metas;

  public:
    /// @brief Set a meta-creation loader for a resource type.
    /// Called by AssetRegistry after all loaders are registered.
    void SetMetaLoader(ResourceType type, IAssetLoader *loader)
    {
        if (loader)
            m_loaders[type] = loader;
    }
};

} // namespace infengine
