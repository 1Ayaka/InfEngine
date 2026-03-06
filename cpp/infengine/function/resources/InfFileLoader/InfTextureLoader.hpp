#pragma once

#include "InfFileLoader.h"

#include <string>
#include <vector>

namespace infengine
{

/// @brief Texture data container holding raw pixel data from stb_image
struct InfTextureData
{
    std::vector<unsigned char> pixels; ///< RGBA pixel data
    int width = 0;                     ///< Image width in pixels
    int height = 0;                    ///< Image height in pixels
    int channels = 4;                  ///< Number of color channels (always forced to 4 for RGBA)
    std::string name;                  ///< Texture identifier/name
    std::string sourcePath;            ///< Original file path

    /// @brief Get total size in bytes
    size_t GetSizeBytes() const
    {
        return static_cast<size_t>(width) * height * channels;
    }

    /// @brief Check if texture data is valid
    bool IsValid() const
    {
        return !pixels.empty() && width > 0 && height > 0;
    }
};

/// @brief Loader for image/texture files using stb_image
/// Supports: PNG, JPG, BMP, TGA, GIF, PSD, HDR, PIC
class InfTextureLoader : public InfFileLoader
{
  public:
    InfTextureLoader();

    /// @brief Load metadata from existing .meta file
    /// @param content The raw file content
    /// @param filePath Path to the texture file
    /// @param metaData Output metadata object
    /// @return true if metadata was loaded successfully
    bool LoadMeta(const char *content, const std::string &filePath, InfResourceMeta &metaData) override;

    /// @brief Create new metadata for a texture file
    /// @param content The raw file content (binary image data)
    /// @param filePath Path to the texture file
    /// @param metaData Output metadata object to populate
    void CreateMeta(const char *content, size_t contentSize, const std::string &filePath,
                    InfResourceMeta &metaData) override;

    /// @brief Load texture data from file content
    /// @param content The raw file content (binary image data)
    /// @param contentSize Size of the content in bytes
    /// @param metaData Metadata for the resource
    /// @return Unique pointer to loaded InfResource containing InfTextureData
    std::unique_ptr<InfResource> Load(const char *content, size_t contentSize, InfResourceMeta &metaData) override;

    /// @brief Load texture directly from file path (convenience method)
    /// @param filePath Path to the texture file
    /// @param name Texture identifier name
    /// @return InfTextureData containing the loaded image
    static InfTextureData LoadFromFile(const std::string &filePath, const std::string &name = "");

    /// @brief Load texture from memory buffer
    /// @param data Pointer to image file data in memory
    /// @param dataSize Size of data in bytes
    /// @param name Texture identifier name
    /// @return InfTextureData containing the loaded image
    static InfTextureData LoadFromMemory(const unsigned char *data, size_t dataSize, const std::string &name = "");

    /// @brief Create a solid color texture (for default/fallback textures)
    /// @param width Texture width
    /// @param height Texture height
    /// @param r Red component (0-255)
    /// @param g Green component (0-255)
    /// @param b Blue component (0-255)
    /// @param a Alpha component (0-255)
    /// @param name Texture identifier name
    /// @return InfTextureData containing the generated texture
    static InfTextureData CreateSolidColor(int width, int height, unsigned char r, unsigned char g, unsigned char b,
                                           unsigned char a, const std::string &name = "solid_color");

    /// @brief Create a checkerboard texture (for error/missing texture indication)
    /// @param width Texture width
    /// @param height Texture height
    /// @param checkerSize Size of each checker square
    /// @param name Texture identifier name
    /// @return InfTextureData containing the generated checkerboard texture
    static InfTextureData CreateCheckerboard(int width, int height, int checkerSize = 8,
                                             const std::string &name = "checkerboard");

  private:
    /// @brief Get texture format type based on file extension
    /// @param extension File extension (e.g., ".png", ".jpg")
    /// @return String describing the texture format
    std::string GetTextureFormatFromExtension(const std::string &extension) const;
};

} // namespace infengine
