#include "InfPythonScriptLoader.hpp"

#include <algorithm>
#include <core/log/InfLog.h>
#include <filesystem>
#include <regex>
#include <sstream>

namespace infengine
{

InfPythonScriptLoader::InfPythonScriptLoader()
{
    INFLOG_DEBUG("InfPythonScriptLoader initialized");
}

bool InfPythonScriptLoader::LoadMeta(const char *content, const std::string &filePath, InfResourceMeta &metaData)
{
    // Try to load existing meta file
    std::string metaPath = InfResourceMeta::GetMetaFilePath(filePath);
    if (std::filesystem::exists(metaPath)) {
        return metaData.LoadFromFile(metaPath);
    }
    return false;
}

void InfPythonScriptLoader::CreateMeta(const char *content, size_t contentSize, const std::string &filePath,
                                       InfResourceMeta &metaData)
{
    INFLOG_DEBUG("Creating metadata for Python script: ", filePath);

    // Initialize with Script type
    metaData.Init(content, contentSize, filePath, ResourceType::Script);

    std::filesystem::path path(filePath);
    std::string extension = path.extension().string();
    std::string contentStr;
    if (content && contentSize > 0) {
        contentStr.assign(content, contentSize);
    }

    // Basic file info
    metaData.AddMetadata("file_type", std::string("script"));
    metaData.AddMetadata("file_extension", extension);
    metaData.AddMetadata("language", std::string("python"));

    // Line count
    size_t lineCount = std::count(contentStr.begin(), contentStr.end(), '\n') + 1;
    metaData.AddMetadata("line_count", lineCount);

    // Parse imports
    std::set<std::string> imports = ParseImports(contentStr);
    std::string importsStr;
    for (const auto &imp : imports) {
        if (!importsStr.empty())
            importsStr += ",";
        importsStr += imp;
    }
    metaData.AddMetadata("imports", importsStr);

    // Parse class names
    std::vector<std::string> classNames = ParseClassNames(contentStr);
    std::string classesStr;
    std::string componentClassesStr;
    for (const auto &cls : classNames) {
        if (!classesStr.empty())
            classesStr += ",";
        classesStr += cls;

        // Check if this is an InfComponent subclass
        if (IsInfComponentClass(contentStr, cls)) {
            if (!componentClassesStr.empty())
                componentClassesStr += ",";
            componentClassesStr += cls;
        }
    }
    metaData.AddMetadata("classes", classesStr);
    metaData.AddMetadata("component_classes", componentClassesStr);

    // Mark if this script contains components
    bool hasComponents = !componentClassesStr.empty();
    metaData.AddMetadata("has_components", hasComponents);

    // File size
    try {
        if (std::filesystem::exists(path)) {
            auto fileSize = std::filesystem::file_size(path);
            metaData.AddMetadata("file_size", static_cast<size_t>(fileSize));
        }
    } catch (const std::filesystem::filesystem_error &e) {
        INFLOG_ERROR("Failed to get file size for ", filePath, " : ", e.what());
    }

    INFLOG_DEBUG("Python script metadata created: ", path.filename().string(), " classes: [", classesStr,
                 "] components: [", componentClassesStr, "]");
}

std::unique_ptr<InfResource> InfPythonScriptLoader::Load(const char *content, size_t contentSize,
                                                         InfResourceMeta &metaData)
{
    // Python scripts are not loaded into C++ memory
    // They are dynamically imported by Python runtime
    // Return nullptr as there's no compiled resource
    INFLOG_DEBUG("Python script load called (no-op, handled by Python runtime)");
    return nullptr;
}

std::set<std::string> InfPythonScriptLoader::ParseImports(const std::string &source) const
{
    std::set<std::string> imports;

    // Process line by line to simulate multiline matching
    std::istringstream stream(source);
    std::string line;
    std::regex importRegex(R"(^\s*(?:import|from)\s+([a-zA-Z_][a-zA-Z0-9_.]*))");

    while (std::getline(stream, line)) {
        std::smatch match;
        if (std::regex_search(line, match, importRegex) && match.size() > 1) {
            std::string moduleName = match[1].str();
            // Get top-level module name (e.g., "InfEngine" from "InfEngine.components")
            size_t dotPos = moduleName.find('.');
            if (dotPos != std::string::npos) {
                moduleName = moduleName.substr(0, dotPos);
            }
            imports.insert(moduleName);
        }
    }

    return imports;
}

std::vector<std::string> InfPythonScriptLoader::ParseClassNames(const std::string &source) const
{
    std::vector<std::string> classNames;

    // Process line by line to simulate multiline matching
    std::istringstream stream(source);
    std::string line;
    std::regex classRegex(R"(^\s*class\s+([a-zA-Z_][a-zA-Z0-9_]*))");

    while (std::getline(stream, line)) {
        std::smatch match;
        if (std::regex_search(line, match, classRegex) && match.size() > 1) {
            classNames.push_back(match[1].str());
        }
    }

    return classNames;
}

bool InfPythonScriptLoader::IsInfComponentClass(const std::string &source, const std::string &className) const
{
    // Look for: class ClassName(InfComponent) or class ClassName(SomethingComponent)
    // Pattern: class <className>(...InfComponent...)
    std::string pattern = R"(class\s+)" + className + R"(\s*\([^)]*InfComponent[^)]*\))";
    std::regex componentRegex(pattern);

    if (std::regex_search(source, componentRegex)) {
        return true;
    }

    // Also check for indirect inheritance patterns commonly used
    // class ClassName(OtherComponent) where OtherComponent might inherit from InfComponent
    // This is a heuristic - we check if parent class name ends with "Component"
    std::string parentPattern = R"(class\s+)" + className + R"(\s*\(\s*([a-zA-Z_][a-zA-Z0-9_]*)\s*\))";
    std::regex parentRegex(parentPattern);
    std::smatch match;

    if (std::regex_search(source, match, parentRegex) && match.size() > 1) {
        std::string parentClass = match[1].str();
        // Heuristic: if parent ends with "Component", assume it's a component
        if (parentClass.length() > 9 && parentClass.substr(parentClass.length() - 9) == "Component") {
            return true;
        }
    }

    return false;
}

} // namespace infengine
