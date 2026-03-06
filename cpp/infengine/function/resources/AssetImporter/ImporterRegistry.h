#pragma once

#include "AssetImporter.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace infengine
{

/**
 * @brief Registry that maps file extensions to AssetImporter instances.
 *
 * Usage:
 *   ImporterRegistry reg;
 *   reg.Register(std::make_unique<TextureImporter>());
 *   auto* imp = reg.GetImporterForExtension(".png");
 */
class ImporterRegistry
{
  public:
    ImporterRegistry() = default;
    ~ImporterRegistry() = default;

    // Non-copyable
    ImporterRegistry(const ImporterRegistry &) = delete;
    ImporterRegistry &operator=(const ImporterRegistry &) = delete;

    /// @brief Register an importer. Extensions are automatically mapped.
    void Register(std::unique_ptr<AssetImporter> importer)
    {
        if (!importer)
            return;

        auto exts = importer->GetSupportedExtensions();
        AssetImporter *raw = importer.get();
        m_importers.push_back(std::move(importer));

        for (const auto &ext : exts) {
            m_extensionMap[ext] = raw;
        }
    }

    /// @brief Get the importer for a given file extension (e.g. ".png")
    /// @return Pointer to the importer, or nullptr if none registered
    [[nodiscard]] AssetImporter *GetImporterForExtension(const std::string &extension) const
    {
        auto it = m_extensionMap.find(extension);
        if (it != m_extensionMap.end())
            return it->second;
        return nullptr;
    }

    /// @brief Get the importer for a given resource type
    [[nodiscard]] AssetImporter *GetImporterForType(ResourceType type) const
    {
        for (const auto &imp : m_importers) {
            if (imp->GetResourceType() == type)
                return imp.get();
        }
        return nullptr;
    }

    /// @brief Get all registered importers
    [[nodiscard]] const std::vector<std::unique_ptr<AssetImporter>> &GetAll() const
    {
        return m_importers;
    }

  private:
    std::vector<std::unique_ptr<AssetImporter>> m_importers;
    std::unordered_map<std::string, AssetImporter *> m_extensionMap;
};

} // namespace infengine
