#include "InfDefaultLoader.hpp"

#include <algorithm>
#include <core/log/InfLog.h>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <vector>

namespace infengine
{
InfDefaultTextLoader::InfDefaultTextLoader()
{
}

bool InfDefaultTextLoader::LoadMeta(const char *content, const std::string &filePath, InfResourceMeta &metaData)
{
    // not implemented yet
    return false;
}

void InfDefaultTextLoader::CreateMeta(const char *content, size_t contentSize, const std::string &filePath,
                                      InfResourceMeta &metaData)
{
    INFLOG_DEBUG("Creating metadata for text file: ", filePath);
    metaData.Init(content, contentSize, filePath, ResourceType::DefaultText);

    std::filesystem::path path(filePath);
    std::string extension = path.extension().string();

    // For text files, analyze content
    std::string contentStr;
    if (content && contentSize > 0) {
        contentStr.assign(content, contentSize);
    }
    size_t lineCount = std::count(contentStr.begin(), contentStr.end(), '\n') + 1;

    // Add metadata specific to text files
    metaData.AddMetadata("file_type", std::string("text"));
    metaData.AddMetadata("file_extension", extension);
    metaData.AddMetadata("is_readable", true);
    metaData.AddMetadata("line_count", lineCount);
    metaData.AddMetadata("character_count", contentStr.length());

    // Check encoding (simple detection)
    bool hasNonAscii = std::any_of(contentStr.begin(), contentStr.end(), [](unsigned char c) { return c > 127; });
    metaData.AddMetadata("encoding", hasNonAscii ? std::string("utf-8") : std::string("ascii"));

    // Add file size information
    try {
        if (std::filesystem::exists(path)) {
            auto fileSize = std::filesystem::file_size(path);
            metaData.AddMetadata("file_size", static_cast<size_t>(fileSize));
        }
    } catch (const std::filesystem::filesystem_error &e) {
        INFLOG_ERROR("Failed to get file size for ", filePath, " : ", e.what());
    }

    INFLOG_DEBUG("Text file metadata created for ", filePath, " lines: ", lineCount, " chars: ", contentStr.length());
}

std::unique_ptr<InfResource> InfDefaultTextLoader::Load(const char *content, size_t contentSize,
                                                        InfResourceMeta &metaData)
{
    return nullptr;
}

// ================== InfDefaultBinaryLoader Implementation ==================

InfDefaultBinaryLoader::InfDefaultBinaryLoader()
{
}

bool InfDefaultBinaryLoader::LoadMeta(const char *content, const std::string &filePath, InfResourceMeta &metaData)
{
    // not implemented yet
    return false;
}

void InfDefaultBinaryLoader::CreateMeta(const char *content, size_t contentSize, const std::string &filePath,
                                        InfResourceMeta &metaData)
{
    metaData.Init(content, contentSize, filePath, ResourceType::DefaultBinary);

    std::filesystem::path path(filePath);
    std::string extension = path.extension().string();

    // Add basic metadata for binary files
    metaData.AddMetadata("file_type", std::string("binary"));
    metaData.AddMetadata("file_extension", extension);
    metaData.AddMetadata("is_readable", false);

    // Determine binary type based on extension only (no content analysis needed)
    std::string binaryType = GetBinaryTypeFromExtension(extension);
    metaData.AddMetadata("binary_type", binaryType);

    // Add file size information
    try {
        if (std::filesystem::exists(path)) {
            auto fileSize = std::filesystem::file_size(path);
            metaData.AddMetadata("file_size", static_cast<size_t>(fileSize));

            // Add size category for binary files
            std::string sizeCategory;
            if (fileSize < 1024) {
                sizeCategory = "tiny";
            } else if (fileSize < 1024 * 1024) {
                sizeCategory = "small";
            } else if (fileSize < 10 * 1024 * 1024) {
                sizeCategory = "medium";
            } else {
                sizeCategory = "large";
            }
            metaData.AddMetadata("size_category", sizeCategory);
        }
    } catch (const std::filesystem::filesystem_error &e) {
        INFLOG_ERROR("Failed to get file size for :", filePath, e.what());
    }

    INFLOG_DEBUG("Binary file metadata created for ", filePath, ", type: ", binaryType);
}

std::unique_ptr<InfResource> InfDefaultBinaryLoader::Load(const char *content, size_t contentSize,
                                                          InfResourceMeta &metaData)
{
    // Binary files are not loaded into memory as InfResource
    // They remain as files on disk and are handled by specialized loaders when needed
    INFLOG_DEBUG("Binary file not loaded into memory: ", metaData.GetDataAs<std::string>("path"));
    return nullptr;
}

std::string InfDefaultBinaryLoader::GetBinaryTypeFromExtension(const std::string &extension) const
{
    // Convert extension to lowercase
    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    // Check common binary types by extension
    if (ext == ".exe" || ext == ".dll" || ext == ".so" || ext == ".dylib") {
        return "executable";
    } else if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" || ext == ".bmp" || ext == ".tiff" ||
               ext == ".ico" || ext == ".webp") {
        return "image";
    } else if (ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".flac" || ext == ".m4a" || ext == ".aac") {
        return "audio";
    } else if (ext == ".mp4" || ext == ".avi" || ext == ".mkv" || ext == ".mov" || ext == ".wmv" || ext == ".flv" ||
               ext == ".webm") {
        return "video";
    } else if (ext == ".zip" || ext == ".rar" || ext == ".7z" || ext == ".tar" || ext == ".gz" || ext == ".bz2" ||
               ext == ".xz") {
        return "archive";
    } else if (ext == ".pdf") {
        return "document";
    } else if (ext == ".ttf" || ext == ".otf" || ext == ".woff" || ext == ".woff2" || ext == ".eot") {
        return "font";
    }

    return "binary";
}

} // namespace infengine