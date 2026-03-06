#pragma once

#include "InfFileLoader.h"
#include <function/resources/InfMaterial/InfMaterial.h>

namespace infengine
{

/**
 * @brief Loader for .mat material files
 *
 * Material files are JSON with the following structure:
 * {
 *   "name": "MaterialName",
 *   "shaders": {
 *     "vertex": "path/to/shader.vert",
 *     "fragment": "path/to/shader.frag"
 *   },
 *   "renderState": { ... },
 *   "properties": { ... }
 * }
 */
class InfMaterialLoader : public InfFileLoader
{
  public:
    InfMaterialLoader() = default;
    ~InfMaterialLoader() = default;

    bool LoadMeta(const char *content, const std::string &filePath, InfResourceMeta &metaData) override;
    void CreateMeta(const char *content, size_t contentSize, const std::string &filePath,
                    InfResourceMeta &metaData) override;
    std::unique_ptr<InfResource> Load(const char *content, size_t contentSize, InfResourceMeta &metaData) override;
};

} // namespace infengine
