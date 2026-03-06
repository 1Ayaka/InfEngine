#include "InfFileManager.h"
#include "InfFileLoader/InfAudioLoader.hpp"
#include "InfFileLoader/InfDefaultLoader.hpp"
#include "InfFileLoader/InfMaterialLoader.hpp"
#include "InfFileLoader/InfPythonScriptLoader.hpp"
#include "InfFileLoader/InfShaderLoader.hpp"
#include "InfFileLoader/InfTextureLoader.hpp"
#include "InfResource/InfResourceMeta.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unordered_set>

namespace infengine
{
InfFileManager::InfFileManager()
{
    m_loaders_umap[ResourceType::Shader] =
        std::make_unique<InfShaderLoader>(true, false, false, false, false, false, false, false, false, false);
    m_loaders_umap[ResourceType::Texture] = std::make_unique<InfTextureLoader>();
    m_loaders_umap[ResourceType::Script] = std::make_unique<InfPythonScriptLoader>();
    m_loaders_umap[ResourceType::Material] = std::make_unique<InfMaterialLoader>();
    m_loaders_umap[ResourceType::Audio] = std::make_unique<InfAudioLoader>();
    m_loaders_umap[ResourceType::DefaultText] = std::make_unique<InfDefaultTextLoader>();
    m_loaders_umap[ResourceType::DefaultBinary] = std::make_unique<InfDefaultBinaryLoader>();
    INFLOG_DEBUG("InfFileManager initialized with Shader, Texture, Script, Material, Audio, Default loaders");
}

bool InfFileManager::ReadFile(const std::string &filePath, std::vector<char> &content) const
{
    bool isBinary = IsBinaryFile(filePath);

    std::ios_base::openmode mode = std::ios::in;
    if (isBinary) {
        mode |= std::ios::binary;
    }

    std::ifstream file(filePath, mode);
    if (!file.is_open()) {
        INFLOG_ERROR("Failed to open file: ", filePath);
        content.clear();
        return false;
    }

    try {
        file.seekg(0, std::ios::end);
        if (file.fail()) {
            INFLOG_ERROR("Failed to seek to end of file: ", filePath);
            content.clear();
            return false;
        }

        std::streampos fileSize = file.tellg();
        if (fileSize == std::streampos(-1)) {
            INFLOG_ERROR("Failed to get file size: ", filePath);
            content.clear();
            return false;
        }

        file.seekg(0, std::ios::beg);
        if (file.fail()) {
            INFLOG_ERROR("Failed to seek to beginning of file: ", filePath);
            content.clear();
            return false;
        }

        content.reserve(static_cast<size_t>(fileSize));
        content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        if (file.bad() || file.fail()) {
            INFLOG_ERROR("Error occurred while reading file: ", filePath);
            content.clear();
            return false;
        }

        return true;
    } catch (const std::exception &e) {
        INFLOG_ERROR("Exception while reading file: ", filePath, " - ", e.what());
        content.clear();
        return false;
    }
}

bool InfFileManager::IsBinaryFile(const std::string &filePath) const
{
    std::filesystem::path path(filePath);
    std::string extension = path.extension().string();

    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    static const std::unordered_set<std::string> textExtensions = {
        ".txt", ".md",  ".json", ".xml",  ".html", ".htm",  ".css", ".js",   ".ts",    ".cpp",  ".c",     ".h",
        ".hpp", ".py",  ".java", ".cs",   ".php",  ".rb",   ".go",  ".rs",   ".swift", ".kt",   ".scala", ".pl",
        ".lua", ".r",   ".sql",  ".yaml", ".yml",  ".toml", ".ini", ".cfg",  ".conf",  ".log",  ".csv",   ".tsv",
        ".rtf", ".tex", ".bib",  ".sh",   ".bat",  ".ps1",  ".cmd", ".vert", ".frag",  ".glsl", ".hlsl",  ".shader"};

    if (textExtensions.find(extension) != textExtensions.end()) {
        return false;
    }

    static const std::unordered_set<std::string> binaryExtensions = {
        ".exe", ".dll",  ".so",  ".dylib", ".bin", ".dat",  ".db",   ".sqlite", ".jpg", ".jpeg",
        ".png", ".gif",  ".bmp", ".tiff",  ".ico", ".webp", ".mp3",  ".wav",    ".ogg", ".flac",
        ".aac", ".m4a",  ".wma", ".mp4",   ".avi", ".mkv",  ".mov",  ".wmv",    ".flv", ".webm",
        ".zip", ".rar",  ".7z",  ".tar",   ".gz",  ".bz2",  ".xz",   ".pdf",    ".doc", ".docx",
        ".xls", ".xlsx", ".ppt", ".pptx",  ".ttf", ".otf",  ".woff", ".woff2",  ".eot"};

    if (binaryExtensions.find(extension) != binaryExtensions.end()) {
        return true;
    }

    return DetectBinaryByContent(filePath);
}

bool InfFileManager::DetectBinaryByContent(const std::string &filePath) const
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    const size_t sampleSize = 512;
    std::vector<char> buffer(sampleSize);
    file.read(buffer.data(), sampleSize);
    size_t bytesRead = file.gcount();

    size_t nullBytes = 0;
    size_t nonAsciiBytes = 0;

    for (size_t i = 0; i < bytesRead; ++i) {
        unsigned char byte = static_cast<unsigned char>(buffer[i]);

        if (byte == 0) {
            nullBytes++;
        } else if (byte > 127) {
            nonAsciiBytes++;
        }
    }

    if (nullBytes > 0) {
        return true;
    }

    double nonAsciiRatio = static_cast<double>(nonAsciiBytes) / bytesRead;
    return nonAsciiRatio > 0.3;
}

bool InfFileManager::ReadFileToContainer(const std::string &filePath, std::vector<char> &content, bool binary) const
{
    std::ios_base::openmode mode = std::ios::in;
    if (binary) {
        mode |= std::ios::binary;
    }

    std::ifstream file(filePath, mode);
    if (!file.is_open()) {
        INFLOG_ERROR("Failed to open file: ", filePath);
        content.clear();
        return false;
    }

    try {
        file.seekg(0, std::ios::end);
        if (file.fail()) {
            INFLOG_ERROR("Failed to seek to end of file: ", filePath);
            content.clear();
            return false;
        }

        std::streampos fileSize = file.tellg();
        if (fileSize == std::streampos(-1)) {
            INFLOG_ERROR("Failed to get file size: ", filePath);
            content.clear();
            return false;
        }

        file.seekg(0, std::ios::beg);
        if (file.fail()) {
            INFLOG_ERROR("Failed to seek to beginning of file: ", filePath);
            content.clear();
            return false;
        }

        content.reserve(static_cast<size_t>(fileSize));
        content.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        if (file.bad() || file.fail()) {
            INFLOG_ERROR("Error occurred while reading file: ", filePath);
            content.clear();
            return false;
        }

        return true;
    } catch (const std::exception &e) {
        INFLOG_ERROR("Exception while reading file: ", filePath, " - ", e.what());
        content.clear();
        return false;
    }
}

ResourceType InfFileManager::GetResourcesType(const std::string &extensionName) const
{
    // Convert extension to lowercase for comparison
    std::string ext = extensionName;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Shader files
    if (ext == ".vert" || ext == ".frag" || ext == ".geom" || ext == ".comp" || ext == ".tesc" || ext == ".tese") {
        return ResourceType::Shader;
    }
    // Material files
    if (ext == ".mat") {
        return ResourceType::Material;
    }
    // Meta files
    if (ext == ".meta") {
        return ResourceType::Meta;
    }
    // Python script files
    if (ext == ".py") {
        return ResourceType::Script;
    }
    // Texture/Image files
    static const std::unordered_set<std::string> textureExtensions = {".png", ".jpg", ".jpeg", ".bmp", ".tga",
                                                                      ".gif", ".psd", ".hdr",  ".pic"};
    if (textureExtensions.find(ext) != textureExtensions.end()) {
        return ResourceType::Texture;
    }
    // Audio files
    static const std::unordered_set<std::string> audioExtensions = {".wav"};
    if (audioExtensions.find(ext) != audioExtensions.end()) {
        return ResourceType::Audio;
    }
    // Text files (known text extensions)
    static const std::unordered_set<std::string> textExtensions = {
        ".txt", ".md",  ".json", ".xml", ".yaml", ".yml", ".toml", ".ini", ".cfg", ".conf", ".html",
        ".htm", ".css", ".js",   ".ts",  ".lua",  ".cs",  ".cpp",  ".c",   ".h",   ".hpp"};
    if (textExtensions.find(ext) != textExtensions.end()) {
        return ResourceType::DefaultText;
    }
    // Binary files (known binary extensions)
    static const std::unordered_set<std::string> binaryExtensions = {
        ".exe", ".dll", ".so",  ".dylib", ".bin",  ".dat", ".fbx", ".obj", ".gltf",
        ".glb", ".wav", ".mp3", ".ogg",   ".flac", ".mp4", ".avi", ".mkv", ".mov",
        ".zip", ".rar", ".7z",  ".tar",   ".gz",   ".pdf", ".ttf", ".otf", ".woff"};
    if (binaryExtensions.find(ext) != binaryExtensions.end()) {
        return ResourceType::DefaultBinary;
    }
    // Default: treat as text
    return ResourceType::DefaultText;
}

ResourceType InfFileManager::GetResourceTypeForPath(const std::string &filePath) const
{
    std::filesystem::path path(filePath);
    std::string ext = path.extension().string();
    return GetResourcesType(ext);
}

std::string InfFileManager::RegisterResource(const std::string &filePath, ResourceType type)
{
    // Log entry point and input values
    INFLOG_DEBUG("Registering resource: filePath = ", filePath, ", type = ", static_cast<int>(type));

    // Check if filePath is empty
    if (filePath.empty()) {
        INFLOG_ERROR("Received empty filePath!");
        return "";
    }

    // Check if resource type is supported
    auto loader = m_loaders_umap.find(type);
    if (loader == m_loaders_umap.end()) {
        INFLOG_ERROR("Resource type not supported: ", static_cast<int>(type));
        return "";
    }
    INFLOG_DEBUG("Resource type supported: ", static_cast<int>(type));

    // Read the file content
    std::vector<char> content;
    if (!ReadFile(filePath, content)) {
        INFLOG_ERROR("Failed to read file for resource registration: ", filePath);
        return "";
    }
    INFLOG_DEBUG("File read successfully, size = ", content.size(), " bytes");

    // if (content.empty()) {
    //     INFLOG_ERROR("File content is empty after reading: ", filePath);
    //     return "";
    // }
    // Not necessary, because the empty file is allowed

    if (content.size() == 0)
        content.emplace_back(0);
    const char *contentPtr = content.data();
    INFLOG_DEBUG("Content pointer obtained, starting meta loading");

    // Try to load metadata from existing .meta file first
    InfResourceMeta metaFile;
    std::string metaFilePath = InfResourceMeta::GetMetaFilePath(filePath);

    if (!metaFile.LoadFromFile(metaFilePath)) {
        // Meta file doesn't exist, try to load from content
        if (!m_loaders_umap[type]->LoadMeta(contentPtr, filePath, metaFile)) {
            // Create new metadata and save it
            m_loaders_umap[type]->CreateMeta(contentPtr, content.size(), filePath, metaFile);
            metaFile.SaveToFile(metaFilePath);
            INFLOG_DEBUG("New metadata created and saved to: ", metaFilePath);
        } else {
            // Metadata loaded from content, save it for next time
            metaFile.SaveToFile(metaFilePath);
            INFLOG_DEBUG("Metadata loaded from content and saved to: ", metaFilePath);
        }
    } else {
        INFLOG_DEBUG("Metadata loaded from existing meta file: ", metaFilePath);
        INFLOG_DEBUG("File Type: ", static_cast<int>(metaFile.GetResourceType()));
    }

    // Log the GUID of the metadata before storing
    std::string guid = metaFile.GetGuid();
    INFLOG_DEBUG("Generated GUID for meta: ", guid);

    // Store the metadata (keyed by GUID)
    m_metas_umap[guid] = std::make_unique<InfResourceMeta>(metaFile);
    // Maintain path → guid reverse index
    m_pathToGuid[filePath] = guid;
    INFLOG_DEBUG("Resource metadata registered with GUID: ", guid);

    return guid;
}

void InfFileManager::LoadResource(const std::string &uid)
{
    auto metaIt = m_metas_umap.find(uid);
    if (metaIt == m_metas_umap.end()) {
        INFLOG_ERROR("Resource meta not found for UID: ", uid);
        return;
    }

    InfResourceMeta *meta = metaIt->second.get();
    ResourceType type = meta->GetResourceType();
    std::string filePath = meta->GetDataAs<std::string>("file_path");

    auto loaderIt = m_loaders_umap.find(type);
    if (loaderIt == m_loaders_umap.end()) {
        INFLOG_ERROR("Resource type not supported for UID: ", uid);
        return;
    }

    std::vector<char> content;
    if (!ReadFile(filePath, content)) {
        INFLOG_ERROR("Failed to read file for resource loading: ", filePath);
        return;
    }

    // Ensure text files are null-terminated for loaders that treat content as C-string
    if (!IsBinaryFile(filePath)) {
        content.push_back('\0');
    }

    std::unique_ptr<InfResource> resource = loaderIt->second->Load(content.data(), content.size(), *meta);
    if (!resource) {
        INFLOG_ERROR("Failed to load resource for UID: ", uid);
        return;
    }

    m_resources_umap[uid] = std::move(resource);
    INFLOG_DEBUG("Resource loaded successfully with UID: ", uid, " and type: ", static_cast<int>(type));
}

void InfFileManager::UnloadResource(const std::string &uid)
{
    auto resourceIt = m_resources_umap.find(uid);
    if (resourceIt != m_resources_umap.end()) {
        m_resources_umap.erase(resourceIt);
        INFLOG_INFO("Resource unloaded successfully with UID: ", uid);
    } else {
        INFLOG_ERROR("Resource not found for UID: ", uid);
    }
}

void InfFileManager::LoadDefaultShaders(const std::filesystem::path &dir, InfRenderer *renderer)
{
    namespace fs = std::filesystem;
    // Track loaded shader_ids to avoid duplicates
    std::unordered_set<std::string> loadedShaderIds;

    // Store shader data to load "unlit" as default after all shaders are processed
    std::vector<char> unlitVertCode;
    std::vector<char> unlitFragCode;

    // use LoadResource to load shaders
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;
        fs::path file = entry.path();
        std::string ext = file.extension().string();

        // Skip .glsl files — they are shader include files resolved by @import,
        // not standalone compilable resources
        if (ext == ".glsl")
            continue;

        ResourceType type = GetResourcesType(ext);

        if (type == ResourceType::Meta)
            continue;

        std::string filePath = file.generic_string();
        std::string uid = RegisterResource(filePath, type);

        // Get shader_id from meta to check for duplicates
        std::string shaderId;
        auto metaIt = m_metas_umap.find(uid);
        if (metaIt != m_metas_umap.end() && metaIt->second) {
            const auto &meta = *(metaIt->second);
            if (meta.HasKey("shader_id")) {
                shaderId = meta.GetDataAs<std::string>("shader_id");
            }
        }

        // If no shader_id found, use filename as default
        if (shaderId.empty()) {
            shaderId = file.filename().string();
        }

        // Create unique key combining shader_id and type (vert/frag)
        std::string shaderKey = shaderId + "_" + ext;

        // Skip if this shader_id + type combination is already loaded
        if (loadedShaderIds.find(shaderKey) != loadedShaderIds.end()) {
            INFLOG_DEBUG("Skipping duplicate shader: ", filePath, " (shader_id: ", shaderId, ")");
            continue;
        }
        loadedShaderIds.insert(shaderKey);

        LoadResource(uid);
        auto compiledPtr = std::static_pointer_cast<std::vector<char>>(m_resources_umap[uid]->GetCompiledData());
        const std::vector<char> &codeData = *compiledPtr;

        // Use shader_id as the primary name for lookup
        INFLOG_DEBUG("Loading shader: ", shaderId, " (", ext, ") from ", filePath);

        // Register with shader_id for material-specific lookup
        renderer->LoadShader(shaderId.c_str(), codeData, ext == ".vert" ? "vertex" : "fragment");

        // Store unlit shader for default registration
        if (shaderId == "unlit") {
            if (ext == ".vert") {
                unlitVertCode = codeData;
            } else if (ext == ".frag") {
                unlitFragCode = codeData;
            }
        }
    }

    // Register "unlit" as the "default" shader
    if (!unlitVertCode.empty()) {
        renderer->LoadShader("default", unlitVertCode, "vertex");
        INFLOG_INFO("Registered 'unlit' as default vertex shader");
    }
    if (!unlitFragCode.empty()) {
        renderer->LoadShader("default", unlitFragCode, "fragment");
        INFLOG_INFO("Registered 'unlit' as default fragment shader");
    }
}

void InfFileManager::LoadAllAssets(const std::filesystem::path &dir)
{
    namespace fs = std::filesystem;
    for (const auto &entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;
        fs::path file = entry.path();
        std::string ext = file.extension().string();
        std::string filePath = file.generic_string();

        // Skip .glsl files — they are shader include files resolved by @import,
        // not standalone compilable resources
        if (ext == ".glsl")
            continue;

        // do not load resources here, for only registering
        ResourceType type = GetResourcesType(ext);

        if (type == ResourceType::Meta)
            continue;
        std::string uid = RegisterResource(filePath, type);
    }
}

void InfFileManager::LoadAssetShaders(const std::filesystem::path &dir, InfRenderer *renderer)
{
    namespace fs = std::filesystem;
    std::unordered_set<std::string> loadedShaderIds;

    for (const auto &entry : fs::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;
        fs::path file = entry.path();
        std::string ext = file.extension().string();

        // Only process shader files
        if (ext != ".vert" && ext != ".frag")
            continue;

        std::string filePath = file.generic_string();

        // Check if already registered
        std::string uid;
        for (const auto &[guid, meta] : m_metas_umap) {
            if (meta && meta->HasKey("file_path")) {
                std::string metaPath = meta->GetDataAs<std::string>("file_path");
                if (metaPath == filePath) {
                    uid = guid;
                    break;
                }
            }
        }

        // Register if not already registered
        if (uid.empty()) {
            uid = RegisterResource(filePath, ResourceType::Shader);
        }

        if (uid.empty())
            continue;

        // Get shader_id from meta
        std::string shaderId;
        auto metaIt = m_metas_umap.find(uid);
        if (metaIt != m_metas_umap.end() && metaIt->second) {
            const auto &meta = *(metaIt->second);
            if (meta.HasKey("shader_id")) {
                shaderId = meta.GetDataAs<std::string>("shader_id");
            }
        }

        if (shaderId.empty()) {
            shaderId = file.stem().string();
        }

        // Create unique key combining shader_id and type
        std::string shaderKey = shaderId + "_" + ext;

        // Skip if already loaded
        if (loadedShaderIds.find(shaderKey) != loadedShaderIds.end()) {
            continue;
        }

        // Skip if already in renderer
        if (renderer->HasShader(shaderId, ext == ".vert" ? "vertex" : "fragment")) {
            loadedShaderIds.insert(shaderKey);
            continue;
        }

        loadedShaderIds.insert(shaderKey);

        // Load and compile the shader
        LoadResource(uid);

        auto resIt = m_resources_umap.find(uid);
        if (resIt == m_resources_umap.end() || !resIt->second)
            continue;

        auto compiledPtr = std::static_pointer_cast<std::vector<char>>(resIt->second->GetCompiledData());
        if (!compiledPtr || compiledPtr->empty())
            continue;

        INFLOG_INFO("Loading asset shader: ", shaderId, " (", ext, ") from ", filePath);
        renderer->LoadShader(shaderId.c_str(), *compiledPtr, ext == ".vert" ? "vertex" : "fragment");
    }
}

void InfFileManager::RemoveResourceMeta(const std::string &uid)
{
    auto metaIt = m_metas_umap.find(uid);
    if (metaIt != m_metas_umap.end()) {
        m_metas_umap.erase(metaIt);
        INFLOG_INFO("Resource meta removed successfully with UID: ", uid);
    } else {
        INFLOG_ERROR("Resource meta not found for UID: ", uid);
    }
}

void InfFileManager::ModifyResource(const std::string &path)
{
    namespace fs = std::filesystem;
    fs::path filePath = path;

    if (!fs::exists(filePath)) {
        INFLOG_WARN("ModifyResource: file does not exist: ", path);
        return;
    }

    std::string ext = filePath.extension().string();
    ResourceType type = GetResourcesType(ext);

    if (type == ResourceType::Meta) {
        return; // Don't process meta files
    }

    std::string metaPath = InfResourceMeta::GetMetaFilePath(path);

    // Read file content
    std::vector<char> content;
    if (!ReadFile(path, content)) {
        INFLOG_ERROR("ModifyResource: failed to read file: ", path);
        return;
    }
    if (content.empty()) {
        content.emplace_back(0);
    }

    // Check if meta exists
    InfResourceMeta meta;
    std::string existingGuid;

    if (fs::exists(metaPath) && meta.LoadFromFile(metaPath)) {
        // Keep existing GUID
        existingGuid = meta.GetGuid();
        if (existingGuid.empty()) {
            existingGuid = meta.GetHashCode();
        }
        INFLOG_DEBUG("ModifyResource: updating existing meta for: ", path);
    }

    // Find loader for this type
    auto loaderIt = m_loaders_umap.find(type);
    if (loaderIt == m_loaders_umap.end()) {
        INFLOG_ERROR("ModifyResource: no loader for type: ", static_cast<int>(type));
        return;
    }

    // Recreate meta with new content
    InfResourceMeta newMeta;
    loaderIt->second->CreateMeta(content.data(), content.size(), path, newMeta);

    // Restore original GUID if it existed (important for maintaining references)
    if (!existingGuid.empty()) {
        newMeta.AddMetadata("guid", existingGuid);
    }

    // Save updated meta
    newMeta.SaveToFile(metaPath);

    // Update cache
    std::string guid = newMeta.GetGuid();
    m_metas_umap[guid] = std::make_unique<InfResourceMeta>(newMeta);
    // Maintain path → guid reverse index
    m_pathToGuid[path] = guid;

    INFLOG_INFO("ModifyResource: updated meta for: ", path, " guid: ", guid);
}

void InfFileManager::DeleteResource(const std::string &path)
{
    namespace fs = std::filesystem;

    // Find and remove from cache by path
    std::string guidToRemove;
    for (const auto &[guid, meta] : m_metas_umap) {
        if (meta->HasKey("file_path")) {
            std::string metaPath = meta->GetDataAs<std::string>("file_path");
            if (metaPath == path) {
                guidToRemove = guid;
                break;
            }
        }
    }

    if (!guidToRemove.empty()) {
        // Unload resource if loaded
        auto resIt = m_resources_umap.find(guidToRemove);
        if (resIt != m_resources_umap.end()) {
            m_resources_umap.erase(resIt);
        }
        // Remove meta from cache
        m_metas_umap.erase(guidToRemove);
        // Remove from path reverse index
        m_pathToGuid.erase(path);
        INFLOG_INFO("DeleteResource: removed from cache: ", path, " guid: ", guidToRemove);
    }

    // Delete .meta file
    std::string metaPath = InfResourceMeta::GetMetaFilePath(path);
    if (fs::exists(metaPath)) {
        fs::remove(metaPath);
        INFLOG_DEBUG("DeleteResource: deleted meta file: ", metaPath);
    }
}

void InfFileManager::MoveResource(const std::string &oldPath, const std::string &newPath)
{
    namespace fs = std::filesystem;

    std::string oldMetaPath = InfResourceMeta::GetMetaFilePath(oldPath);
    std::string newMetaPath = InfResourceMeta::GetMetaFilePath(newPath);

    // Load existing meta
    InfResourceMeta meta;
    std::string existingGuid;

    if (fs::exists(oldMetaPath) && meta.LoadFromFile(oldMetaPath)) {
        existingGuid = meta.GetGuid();

        // Update file path in meta (keep same GUID!)
        meta.UpdateFilePath(newPath);

        // Save to new location
        meta.SaveToFile(newMetaPath);

        // Delete old meta file
        fs::remove(oldMetaPath);

        // Update cache: remove old entry, add with same guid
        auto it = m_metas_umap.find(existingGuid);
        if (it != m_metas_umap.end()) {
            it->second->UpdateFilePath(newPath);
        }

        // Update path reverse index
        m_pathToGuid.erase(oldPath);
        m_pathToGuid[newPath] = existingGuid;

        INFLOG_INFO("MoveResource: ", oldPath, " -> ", newPath, " (guid preserved: ", existingGuid, ")");
    } else {
        // No existing meta, just register the new file
        std::string ext = fs::path(newPath).extension().string();
        ResourceType type = GetResourcesType(ext);

        if (type != ResourceType::Meta) {
            RegisterResource(newPath, type);
            INFLOG_INFO("MoveResource: registered new resource at: ", newPath);
        }
    }
}

const InfResourceMeta *InfFileManager::GetMetaByGuid(const std::string &guid) const
{
    auto it = m_metas_umap.find(guid);
    if (it != m_metas_umap.end()) {
        return it->second.get();
    }
    return nullptr;
}

const InfResourceMeta *InfFileManager::GetMetaByPath(const std::string &filePath) const
{
    // O(1) lookup via reverse index
    auto pathIt = m_pathToGuid.find(filePath);
    if (pathIt != m_pathToGuid.end()) {
        auto metaIt = m_metas_umap.find(pathIt->second);
        if (metaIt != m_metas_umap.end()) {
            return metaIt->second.get();
        }
    }
    // Fallback: linear scan for path normalization differences
    for (const auto &[guid, meta] : m_metas_umap) {
        if (meta->HasKey("file_path")) {
            std::string metaPath = meta->GetDataAs<std::string>("file_path");
            if (metaPath == filePath) {
                return meta.get();
            }
        }
    }
    return nullptr;
}

std::vector<std::string> InfFileManager::GetAllResourceGuids() const
{
    std::vector<std::string> guids;
    guids.reserve(m_metas_umap.size());
    for (const auto &[guid, meta] : m_metas_umap) {
        guids.push_back(guid);
    }
    return guids;
}

const InfResource *InfFileManager::GetResource(const std::string &uid) const
{
    auto it = m_resources_umap.find(uid);
    if (it != m_resources_umap.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::string InfFileManager::FindShaderPathById(const std::string &shaderId, const std::string &shaderType) const
{
    // Determine expected extension
    std::string expectedExt;
    if (shaderType == "vertex" || shaderType == ".vert" || shaderType == "vert") {
        expectedExt = ".vert";
    } else if (shaderType == "fragment" || shaderType == ".frag" || shaderType == "frag") {
        expectedExt = ".frag";
    } else {
        return "";
    }

    // Search through all registered metas for matching shader_id
    for (const auto &[guid, meta] : m_metas_umap) {
        if (!meta)
            continue;

        // Check resource type
        if (!meta->HasKey("type"))
            continue;
        std::string type = meta->GetDataAs<std::string>("type");
        bool matchesType =
            (expectedExt == ".vert" && type == "vertex") || (expectedExt == ".frag" && type == "fragment");
        if (!matchesType)
            continue;

        // Check shader_id
        if (meta->HasKey("shader_id")) {
            std::string metaShaderId = meta->GetDataAs<std::string>("shader_id");
            if (metaShaderId == shaderId) {
                if (meta->HasKey("file_path")) {
                    return meta->GetDataAs<std::string>("file_path");
                }
            }
        }
    }

    return "";
}

} // namespace infengine