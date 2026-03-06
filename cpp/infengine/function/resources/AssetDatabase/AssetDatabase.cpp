#include "AssetDatabase.h"

#include <core/log/InfLog.h>

#include <algorithm>

namespace infengine
{

AssetDatabase::AssetDatabase(InfFileManager *fileManager) : m_fileManager(fileManager)
{
}

void AssetDatabase::SetFileManager(InfFileManager *fileManager)
{
    m_fileManager = fileManager;
}

void AssetDatabase::Initialize(const std::string &projectRoot)
{
    m_projectRoot = std::filesystem::path(projectRoot).generic_string();
    if (!m_projectRoot.empty() && m_projectRoot.back() == '/') {
        m_projectRoot.pop_back();
    }

    std::filesystem::path assetsPath = std::filesystem::path(m_projectRoot) / "Assets";
    if (std::filesystem::exists(assetsPath)) {
        m_assetsRoot = assetsPath.generic_string();
    } else {
        // Fallback to project root if Assets folder missing
        m_assetsRoot = m_projectRoot;
    }

    INFLOG_DEBUG("AssetDatabase initialized. ProjectRoot=", m_projectRoot, ", AssetsRoot=", m_assetsRoot);
}

void AssetDatabase::Refresh()
{
    if (!m_fileManager) {
        INFLOG_WARN("AssetDatabase.Refresh called without file manager");
        return;
    }

    m_guidToPath.clear();
    m_pathToGuid.clear();

    if (m_assetsRoot.empty()) {
        INFLOG_WARN("AssetDatabase.Refresh: assets root not set");
        return;
    }

    std::filesystem::path assetsRootPath(m_assetsRoot);
    if (!std::filesystem::exists(assetsRootPath)) {
        INFLOG_WARN("AssetDatabase.Refresh: assets root does not exist: ", m_assetsRoot);
        return;
    }

    for (const auto &entry : std::filesystem::recursive_directory_iterator(assetsRootPath)) {
        if (!entry.is_regular_file())
            continue;

        const std::filesystem::path filePath = entry.path();
        if (IsMetaFile(filePath))
            continue;

        ImportAsset(filePath.generic_string());
    }

    INFLOG_INFO("AssetDatabase.Refresh completed. Total assets: ", m_guidToPath.size());
}

std::string AssetDatabase::ImportAsset(const std::string &path)
{
    if (!m_fileManager) {
        INFLOG_WARN("AssetDatabase.ImportAsset: file manager not set");
        return "";
    }

    std::filesystem::path fsPath(path);
    if (!std::filesystem::exists(fsPath) || !std::filesystem::is_regular_file(fsPath)) {
        return "";
    }

    if (IsMetaFile(fsPath)) {
        return "";
    }

    ResourceType type = m_fileManager->GetResourceTypeForPath(path);
    if (type == ResourceType::Meta) {
        return "";
    }

    std::string guid = m_fileManager->RegisterResource(path, type);
    if (guid.empty()) {
        return "";
    }

    UpdateMapping(guid, path);
    return guid;
}

bool AssetDatabase::DeleteAsset(const std::string &path)
{
    if (!m_fileManager) {
        return false;
    }

    std::string guid = GetGuidFromPath(path);
    m_fileManager->DeleteResource(path);

    if (!guid.empty()) {
        RemoveMappingByGuid(guid);
    } else {
        RemoveMappingByPath(path);
    }
    return true;
}

bool AssetDatabase::MoveAsset(const std::string &oldPath, const std::string &newPath)
{
    if (!m_fileManager) {
        return false;
    }

    std::string guid = GetGuidFromPath(oldPath);
    if (guid.empty()) {
        // Try to recover guid from old .meta file
        const std::string metaPath = InfResourceMeta::GetMetaFilePath(oldPath);
        if (std::filesystem::exists(metaPath)) {
            InfResourceMeta meta;
            if (meta.LoadFromFile(metaPath)) {
                guid = meta.GetGuid();
            }
        }
    }
    m_fileManager->MoveResource(oldPath, newPath);

    if (!guid.empty()) {
        UpdateMapping(guid, newPath);
        RemoveMappingByPath(oldPath);
        return true;
    }

    // If GUID not found, attempt to re-import
    std::string newGuid = ImportAsset(newPath);
    return !newGuid.empty();
}

bool AssetDatabase::ContainsGuid(const std::string &guid) const
{
    return m_guidToPath.find(guid) != m_guidToPath.end();
}

bool AssetDatabase::ContainsPath(const std::string &path) const
{
    std::string norm = NormalizePath(path);
    return m_pathToGuid.find(norm) != m_pathToGuid.end();
}

std::string AssetDatabase::GetGuidFromPath(const std::string &path) const
{
    std::string norm = NormalizePath(path);
    auto it = m_pathToGuid.find(norm);
    if (it != m_pathToGuid.end()) {
        return it->second;
    }

    // Try to load from meta file directly
    const std::string metaPath = InfResourceMeta::GetMetaFilePath(path);
    if (std::filesystem::exists(metaPath)) {
        InfResourceMeta meta;
        if (meta.LoadFromFile(metaPath)) {
            return meta.GetGuid();
        }
    }

    // Try to read from meta if available
    if (m_fileManager) {
        if (const InfResourceMeta *meta = m_fileManager->GetMetaByPath(path)) {
            return meta->GetGuid();
        }
    }

    return "";
}

std::string AssetDatabase::GetPathFromGuid(const std::string &guid) const
{
    auto it = m_guidToPath.find(guid);
    if (it != m_guidToPath.end()) {
        return it->second;
    }

    if (m_fileManager) {
        if (const InfResourceMeta *meta = m_fileManager->GetMetaByGuid(guid)) {
            if (meta->HasKey("file_path")) {
                return meta->GetDataAs<std::string>("file_path");
            }
        }
    }

    return "";
}

const InfResourceMeta *AssetDatabase::GetMetaByGuid(const std::string &guid) const
{
    if (!m_fileManager)
        return nullptr;
    return m_fileManager->GetMetaByGuid(guid);
}

const InfResourceMeta *AssetDatabase::GetMetaByPath(const std::string &path) const
{
    if (!m_fileManager)
        return nullptr;
    return m_fileManager->GetMetaByPath(path);
}

std::vector<std::string> AssetDatabase::GetAllGuids() const
{
    std::vector<std::string> result;
    result.reserve(m_guidToPath.size());
    for (const auto &pair : m_guidToPath) {
        result.push_back(pair.first);
    }
    return result;
}

bool AssetDatabase::IsAssetPath(const std::string &path) const
{
    if (m_assetsRoot.empty())
        return false;

    std::string norm = NormalizePath(path);
    std::string assetsNorm = NormalizePath(m_assetsRoot);

    if (assetsNorm.empty())
        return false;

    if (norm.size() < assetsNorm.size())
        return false;

    return norm.rfind(assetsNorm, 0) == 0;
}

void AssetDatabase::OnAssetCreated(const std::string &path)
{
    ImportAsset(path);
}

void AssetDatabase::OnAssetModified(const std::string &path)
{
    if (!m_fileManager) {
        return;
    }

    m_fileManager->ModifyResource(path);

    // Update mapping if needed
    std::string guid = GetGuidFromPath(path);
    if (guid.empty()) {
        ImportAsset(path);
    } else {
        UpdateMapping(guid, path);
    }
}

void AssetDatabase::OnAssetDeleted(const std::string &path)
{
    DeleteAsset(path);
}

void AssetDatabase::OnAssetMoved(const std::string &oldPath, const std::string &newPath)
{
    MoveAsset(oldPath, newPath);
}

std::string AssetDatabase::NormalizePath(const std::string &path) const
{
    if (path.empty())
        return "";

    try {
        std::filesystem::path fsPath(path);
        std::filesystem::path normPath;
        if (std::filesystem::exists(fsPath)) {
            normPath = std::filesystem::weakly_canonical(fsPath);
        } else {
            normPath = fsPath.lexically_normal();
        }
        std::string result = normPath.generic_string();

#ifdef _WIN32
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
#endif

        return result;
    } catch (...) {
        std::string result = std::filesystem::path(path).generic_string();
#ifdef _WIN32
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
#endif
        return result;
    }
}

bool AssetDatabase::IsMetaFile(const std::filesystem::path &path) const
{
    return path.extension().string() == ".meta";
}

void AssetDatabase::UpdateMapping(const std::string &guid, const std::string &path)
{
    if (guid.empty() || path.empty())
        return;

    std::string norm = NormalizePath(path);
    m_guidToPath[guid] = path;
    m_pathToGuid[norm] = guid;
}

void AssetDatabase::RemoveMappingByGuid(const std::string &guid)
{
    auto it = m_guidToPath.find(guid);
    if (it != m_guidToPath.end()) {
        RemoveMappingByPath(it->second);
        m_guidToPath.erase(it);
    }
}

void AssetDatabase::RemoveMappingByPath(const std::string &path)
{
    std::string norm = NormalizePath(path);
    auto it = m_pathToGuid.find(norm);
    if (it != m_pathToGuid.end()) {
        m_pathToGuid.erase(it);
    }
}

} // namespace infengine
