#pragma once

#include <function/resources/InfFileManager.h>
#include <function/resources/InfResource/InfResourceMeta.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace infengine
{

/**
 * @brief Central asset database for the project.
 *
 * Responsibilities:
 * - Import assets and generate .meta files
 * - Maintain GUID <-> path mappings
 * - Provide Asset CRUD operations for editor and file watcher
 */
class AssetDatabase
{
  public:
    AssetDatabase() = default;
    explicit AssetDatabase(InfFileManager *fileManager);

    /// @brief Set the file manager used for import/metadata operations
    void SetFileManager(InfFileManager *fileManager);

    /// @brief Initialize database with project root path
    void Initialize(const std::string &projectRoot);

    /// @brief Refresh all assets by scanning the Assets folder
    void Refresh();

    /// @brief Import an asset and create/update its meta
    /// @return GUID of the asset, empty if failed
    std::string ImportAsset(const std::string &path);

    /// @brief Delete asset and meta
    bool DeleteAsset(const std::string &path);

    /// @brief Move/rename asset preserving GUID
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

    // ========================================================================
    // File watcher hooks
    // ========================================================================

    void OnAssetCreated(const std::string &path);
    void OnAssetModified(const std::string &path);
    void OnAssetDeleted(const std::string &path);
    void OnAssetMoved(const std::string &oldPath, const std::string &newPath);

  private:
    [[nodiscard]] std::string NormalizePath(const std::string &path) const;
    [[nodiscard]] bool IsMetaFile(const std::filesystem::path &path) const;
    void UpdateMapping(const std::string &guid, const std::string &path);
    void RemoveMappingByGuid(const std::string &guid);
    void RemoveMappingByPath(const std::string &path);

    InfFileManager *m_fileManager = nullptr;
    std::string m_projectRoot;
    std::string m_assetsRoot;

    // GUID -> path
    std::unordered_map<std::string, std::string> m_guidToPath;
    // normalized path -> GUID
    std::unordered_map<std::string, std::string> m_pathToGuid;
};

} // namespace infengine
