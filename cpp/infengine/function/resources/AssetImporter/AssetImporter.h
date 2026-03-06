#pragma once

#include <core/types/InfFwdType.h>

#include <string>
#include <vector>

namespace infengine
{

class InfResourceMeta;

/**
 * @brief Import context passed to each AssetImporter.
 *
 * Contains information about the asset being imported and
 * accessors for the meta / import settings.
 */
struct ImportContext
{
    std::string sourcePath;          ///< Absolute path of the source asset file
    std::string guid;                ///< GUID assigned by AssetDatabase
    ResourceType resourceType;       ///< Detected resource type
    InfResourceMeta *meta = nullptr; ///< Meta object (read/write)
    bool isReimport = false;         ///< true when re-importing an existing asset
};

/**
 * @brief Abstract base for asset importers.
 *
 * Each concrete importer handles one category of resource
 * (textures, shaders, materials, …).  The ImporterRegistry
 * maps file extensions to their importer, and AssetDatabase
 * calls Import() / Reimport() during the asset pipeline.
 *
 * Importers are thin wrappers that delegate heavy work to
 * the existing InfFileLoader subclasses.
 */
class AssetImporter
{
  public:
    virtual ~AssetImporter() = default;

    /// @brief Resource type this importer handles
    [[nodiscard]] virtual ResourceType GetResourceType() const = 0;

    /// @brief File extensions this importer supports (e.g. {".png", ".jpg"})
    [[nodiscard]] virtual std::vector<std::string> GetSupportedExtensions() const = 0;

    /// @brief Import an asset for the first time (or overwrite)
    /// @param ctx Import context with source path, meta, etc.
    /// @return true if import succeeded
    virtual bool Import(const ImportContext &ctx) = 0;

    /// @brief Re-import an existing asset (e.g. after settings changed)
    /// @param ctx Import context (isReimport = true)
    /// @return true if reimport succeeded
    virtual bool Reimport(const ImportContext &ctx)
    {
        // Default: just re-run Import
        return Import(ctx);
    }

    /// @brief Called after meta is loaded, before Import. Allows the importer
    ///        to fill default import settings if missing.
    virtual void EnsureDefaultSettings(InfResourceMeta & /*meta*/)
    {
        // Override in concrete importers to populate import_settings
    }
};

} // namespace infengine
