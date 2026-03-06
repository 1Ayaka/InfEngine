#include "InfTextureLoader.hpp"

#include <algorithm>
#include <core/log/InfLog.h>
#include <filesystem>
#include <stb_image.h>

namespace infengine
{

InfTextureLoader::InfTextureLoader()
{
    INFLOG_DEBUG("InfTextureLoader initialized");
}

bool InfTextureLoader::LoadMeta(const char *content, const std::string &filePath, InfResourceMeta &metaData)
{
    std::string metaPath = InfResourceMeta::GetMetaFilePath(filePath);
    if (std::filesystem::exists(metaPath)) {
        return metaData.LoadFromFile(metaPath);
    }
    return false;
}

void InfTextureLoader::CreateMeta(const char *content, size_t contentSize, const std::string &filePath,
                                  InfResourceMeta &metaData)
{
    INFLOG_DEBUG("Creating metadata for texture: ", filePath);
    metaData.Init(content, contentSize, filePath, ResourceType::Texture);

    std::filesystem::path path(filePath);
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

    // Get image dimensions without fully loading it
    int width = 0, height = 0, channels = 0;
    if (stbi_info(filePath.c_str(), &width, &height, &channels)) {
        metaData.AddMetadata("width", width);
        metaData.AddMetadata("height", height);
        metaData.AddMetadata("channels", channels);
    } else {
        INFLOG_WARN("Could not read image info for: ", filePath);
    }

    // Add common metadata
    metaData.AddMetadata("file_type", std::string("texture"));
    metaData.AddMetadata("file_extension", extension);
    metaData.AddMetadata("texture_format", GetTextureFormatFromExtension(extension));
    metaData.AddMetadata("is_binary", true);

    // File size
    try {
        if (std::filesystem::exists(path)) {
            auto fileSize = std::filesystem::file_size(path);
            metaData.AddMetadata("file_size", static_cast<size_t>(fileSize));
        }
    } catch (const std::filesystem::filesystem_error &e) {
        INFLOG_ERROR("Failed to get file size for ", filePath, " : ", e.what());
    }

    INFLOG_DEBUG("Texture metadata created: ", path.filename().string(), " [", width, "x", height, "x", channels, "]");
}

std::unique_ptr<InfResource> InfTextureLoader::Load(const char *content, size_t contentSize, InfResourceMeta &metaData)
{
    std::string name = metaData.GetResourceName();
    std::string filePath = metaData.GetDataAs<std::string>("file_path");

    InfTextureData texData = LoadFromFile(filePath, name);
    if (!texData.IsValid()) {
        INFLOG_ERROR("Failed to load texture: ", filePath);
        // Create fallback checkerboard texture
        texData = CreateCheckerboard(64, 64, 8, name + "_fallback");
    }

    // Create resource with texture data
    auto rawData = std::make_shared<InfTextureData>(std::move(texData));
    // CompiledData will be set later by renderer when uploading to GPU
    auto compiledData = std::shared_ptr<void>(nullptr);

    return std::make_unique<InfResource>(ResourceType::Texture, rawData, compiledData);
}

InfTextureData InfTextureLoader::LoadFromFile(const std::string &filePath, const std::string &name)
{
    InfTextureData result;
    result.sourcePath = filePath;
    result.name = name.empty() ? std::filesystem::path(filePath).stem().string() : name;

    int width, height, channels;
    // Force 4 channels (RGBA) for consistent Vulkan format
    stbi_uc *pixels = stbi_load(filePath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        INFLOG_ERROR("stbi_load failed for: ", filePath, " - ", stbi_failure_reason());
        return result;
    }

    result.width = width;
    result.height = height;
    result.channels = 4; // Always RGBA
    size_t dataSize = static_cast<size_t>(width) * height * 4;
    result.pixels.assign(pixels, pixels + dataSize);

    stbi_image_free(pixels);

    INFLOG_DEBUG("Loaded texture: ", result.name, " [", width, "x", height, "]");
    return result;
}

InfTextureData InfTextureLoader::LoadFromMemory(const unsigned char *data, size_t dataSize, const std::string &name)
{
    InfTextureData result;
    result.name = name;

    int width, height, channels;
    stbi_uc *pixels =
        stbi_load_from_memory(data, static_cast<int>(dataSize), &width, &height, &channels, STBI_rgb_alpha);

    if (!pixels) {
        INFLOG_ERROR("stbi_load_from_memory failed: ", stbi_failure_reason());
        return result;
    }

    result.width = width;
    result.height = height;
    result.channels = 4;
    size_t pixelDataSize = static_cast<size_t>(width) * height * 4;
    result.pixels.assign(pixels, pixels + pixelDataSize);

    stbi_image_free(pixels);

    INFLOG_DEBUG("Loaded texture from memory: ", result.name, " [", width, "x", height, "]");
    return result;
}

InfTextureData InfTextureLoader::CreateSolidColor(int width, int height, unsigned char r, unsigned char g,
                                                  unsigned char b, unsigned char a, const std::string &name)
{
    InfTextureData result;
    result.name = name;
    result.width = width;
    result.height = height;
    result.channels = 4;
    result.pixels.resize(static_cast<size_t>(width) * height * 4);

    for (int i = 0; i < width * height; ++i) {
        result.pixels[i * 4 + 0] = r;
        result.pixels[i * 4 + 1] = g;
        result.pixels[i * 4 + 2] = b;
        result.pixels[i * 4 + 3] = a;
    }

    INFLOG_DEBUG("Created solid color texture: ", name, " [", width, "x", height, "] RGBA(", (int)r, ",", (int)g, ",",
                 (int)b, ",", (int)a, ")");
    return result;
}

InfTextureData InfTextureLoader::CreateCheckerboard(int width, int height, int checkerSize, const std::string &name)
{
    InfTextureData result;
    result.name = name;
    result.width = width;
    result.height = height;
    result.channels = 4;
    result.pixels.resize(static_cast<size_t>(width) * height * 4);

    // Magenta and black checkerboard (classic "missing texture" pattern)
    const unsigned char color1[4] = {255, 0, 255, 255}; // Magenta
    const unsigned char color2[4] = {0, 0, 0, 255};     // Black

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int checkerX = x / checkerSize;
            int checkerY = y / checkerSize;
            const unsigned char *color = ((checkerX + checkerY) % 2 == 0) ? color1 : color2;

            int idx = (y * width + x) * 4;
            result.pixels[idx + 0] = color[0];
            result.pixels[idx + 1] = color[1];
            result.pixels[idx + 2] = color[2];
            result.pixels[idx + 3] = color[3];
        }
    }

    INFLOG_DEBUG("Created checkerboard texture: ", name, " [", width, "x", height, "] checker size: ", checkerSize);
    return result;
}

std::string InfTextureLoader::GetTextureFormatFromExtension(const std::string &extension) const
{
    static const std::unordered_map<std::string, std::string> formatMap = {
        {".png", "PNG"}, {".jpg", "JPEG"}, {".jpeg", "JPEG"}, {".bmp", "BMP"}, {".tga", "TGA"}, {".gif", "GIF"},
        {".psd", "PSD"}, {".hdr", "HDR"},  {".pic", "PIC"},   {".pnm", "PNM"}, {".pgm", "PGM"}, {".ppm", "PPM"},
    };

    std::string ext = extension;
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    auto it = formatMap.find(ext);
    if (it != formatMap.end()) {
        return it->second;
    }
    return "Unknown";
}

} // namespace infengine
