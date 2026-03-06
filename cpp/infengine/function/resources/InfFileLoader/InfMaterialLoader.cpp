#include "InfMaterialLoader.hpp"
#include <core/log/InfLog.h>
#include <filesystem>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace infengine
{

bool InfMaterialLoader::LoadMeta(const char *content, const std::string &filePath, InfResourceMeta &metaData)
{
    INFLOG_DEBUG("Loading material with metadata from file: ", filePath);
    // not implemented yet - material meta loading
    return false;
}

void InfMaterialLoader::CreateMeta(const char *content, size_t contentSize, const std::string &filePath,
                                   InfResourceMeta &metaData)
{
    if (!content) {
        INFLOG_ERROR("Invalid material content for metadata creation");
        return;
    }

    metaData.Init(content, contentSize, filePath, ResourceType::Material);

    // Try to extract material name from JSON content
    try {
        std::string jsonStr(content, contentSize);
        json j = json::parse(jsonStr);
        if (j.contains("name")) {
            metaData.AddMetadata("material_name", j["name"].get<std::string>());
        }
    } catch (...) {
        // Fallback to filename
        std::filesystem::path path(filePath);
        metaData.AddMetadata("material_name", path.stem().string());
    }

    INFLOG_DEBUG("Material metadata created for file: ", filePath);
}

std::unique_ptr<InfResource> InfMaterialLoader::Load(const char *content, size_t contentSize, InfResourceMeta &metaData)
{
    if (!content) {
        INFLOG_ERROR("Invalid material content");
        return nullptr;
    }

    std::string jsonStr(content);

    // Create InfMaterial and deserialize
    auto material = std::make_shared<InfMaterial>();
    if (!material->Deserialize(jsonStr)) {
        INFLOG_ERROR("Failed to deserialize material from: ", metaData.GetDataAs<std::string>("file_path"));
        return nullptr;
    }

    // Set GUID from meta
    material->SetGuid(metaData.GetGuid());

    // Set file path so material can be saved back to the same location
    std::string filePath = metaData.GetDataAs<std::string>("file_path");
    if (!filePath.empty()) {
        material->SetFilePath(filePath);
    }

    INFLOG_DEBUG("Material loaded successfully: ", material->GetName(), " from: ", filePath);

    // Store the material in raw data, compiled data can be pipeline info later
    auto rawData = new std::shared_ptr<InfMaterial>(material);
    auto compiledData = new std::shared_ptr<InfMaterial>(material); // Same for now

    return MakeResource<ResourceType::Material>(rawData, compiledData);
}

} // namespace infengine
