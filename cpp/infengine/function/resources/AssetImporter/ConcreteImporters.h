#pragma once

#include "AssetImporter.h"

namespace infengine
{

// ==========================================================================
// TextureImporter
// ==========================================================================

class TextureImporter final : public AssetImporter
{
  public:
    [[nodiscard]] ResourceType GetResourceType() const override
    {
        return ResourceType::Texture;
    }

    [[nodiscard]] std::vector<std::string> GetSupportedExtensions() const override
    {
        return {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif", ".psd", ".hdr", ".pic"};
    }

    bool Import(const ImportContext &ctx) override
    {
        // Texture loading is handled by InfTextureLoader via InfFileManager::LoadResource().
        // This Import() ensures the meta file is created (already done by RegisterResource).
        if (!ctx.meta)
            return false;
        EnsureDefaultSettings(*ctx.meta);
        return true;
    }

    void EnsureDefaultSettings(InfResourceMeta &meta) override
    {
        if (!meta.HasKey("wrap_mode"))
            meta.AddMetadata("wrap_mode", std::string("repeat"));
        if (!meta.HasKey("filter_mode"))
            meta.AddMetadata("filter_mode", std::string("linear"));
        if (!meta.HasKey("generate_mipmaps"))
            meta.AddMetadata("generate_mipmaps", true);
        if (!meta.HasKey("srgb"))
            meta.AddMetadata("srgb", true);
        if (!meta.HasKey("max_size"))
            meta.AddMetadata("max_size", 2048);
    }
};

// ==========================================================================
// ShaderImporter
// ==========================================================================

class ShaderImporter final : public AssetImporter
{
  public:
    [[nodiscard]] ResourceType GetResourceType() const override
    {
        return ResourceType::Shader;
    }

    [[nodiscard]] std::vector<std::string> GetSupportedExtensions() const override
    {
        return {".vert", ".frag", ".geom", ".comp", ".tesc", ".tese"};
    }

    bool Import(const ImportContext &ctx) override
    {
        if (!ctx.meta)
            return false;
        EnsureDefaultSettings(*ctx.meta);
        return true;
    }

    void EnsureDefaultSettings(InfResourceMeta & /*meta*/) override
    {
        // Shader-specific settings can be added later (e.g. optimization level)
    }
};

// ==========================================================================
// MaterialImporter
// ==========================================================================

class MaterialImporter final : public AssetImporter
{
  public:
    [[nodiscard]] ResourceType GetResourceType() const override
    {
        return ResourceType::Material;
    }

    [[nodiscard]] std::vector<std::string> GetSupportedExtensions() const override
    {
        return {".mat"};
    }

    bool Import(const ImportContext &ctx) override
    {
        if (!ctx.meta)
            return false;
        return true;
    }
};

// ==========================================================================
// ScriptImporter
// ==========================================================================

class ScriptImporter final : public AssetImporter
{
  public:
    [[nodiscard]] ResourceType GetResourceType() const override
    {
        return ResourceType::Script;
    }

    [[nodiscard]] std::vector<std::string> GetSupportedExtensions() const override
    {
        return {".py"};
    }

    bool Import(const ImportContext &ctx) override
    {
        if (!ctx.meta)
            return false;
        return true;
    }
};

// ==========================================================================
// AudioImporter
// ==========================================================================

class AudioImporter final : public AssetImporter
{
  public:
    [[nodiscard]] ResourceType GetResourceType() const override
    {
        return ResourceType::Audio;
    }

    [[nodiscard]] std::vector<std::string> GetSupportedExtensions() const override
    {
        return {".wav"};
    }

    bool Import(const ImportContext &ctx) override
    {
        if (!ctx.meta)
            return false;
        EnsureDefaultSettings(*ctx.meta);
        return true;
    }

    void EnsureDefaultSettings(InfResourceMeta &meta) override
    {
        if (!meta.HasKey("force_mono"))
            meta.AddMetadata("force_mono", false);
        if (!meta.HasKey("load_in_background"))
            meta.AddMetadata("load_in_background", false);
        if (!meta.HasKey("quality"))
            meta.AddMetadata("quality", 1.0f);
    }
};

} // namespace infengine
