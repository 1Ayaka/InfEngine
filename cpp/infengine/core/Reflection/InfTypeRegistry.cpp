#include "InfTypeRegistry.h"

#include <core/log/InfLog.h>
#include <core/types/InfFwdType.h>

#include <any>
#include <functional>
#include <string>
#include <typeindex>

namespace infengine
{
InfTypeRegistry &InfTypeRegistry::GetInstance()
{
    static InfTypeRegistry instance;
    return instance;
}

std::any InfTypeRegistry::Create(const std::string &typeName) const
{
    auto it = m_typeConstructors.find(typeName);
    if (it != m_typeConstructors.end()) {
        return it->second();
    }
    INFLOG_ERROR("Type not registered: ", typeName);
    return std::any{};
}

std::string InfTypeRegistry::GetTypeName(std::type_index typeIndex) const
{
    auto it = m_typeNames.find(typeIndex);
    if (it != m_typeNames.end()) {
        return it->second;
    }
    INFLOG_ERROR("Type not registered: ", typeIndex.name());
    return {};
}

std::string InfTypeRegistry::ToString(const std::string &typeName, const std::any &value) const
{
    auto it = m_toStringFuncs.find(typeName);
    if (it != m_toStringFuncs.end()) {
        return it->second(value);
    }
    INFLOG_ERROR("ToString function not registered for type: ", typeName);
    return {};
}

std::any InfTypeRegistry::FromString(const std::string &typeName, const std::string &str) const
{
    auto it = m_fromStringFuncs.find(typeName);
    if (it != m_fromStringFuncs.end()) {
        return it->second(str);
    }
    INFLOG_ERROR("FromString function not registered for type: ", typeName);
    return {};
}

InfTypeRegistry::InfTypeRegistry()
{
    Build();
}

void InfTypeRegistry::Build()
{
    // Register built-in types and their conversion functions here
    RegisterType<int>(
        "int", [](const std::any &v) { return std::to_string(std::any_cast<int>(v)); },
        [](const std::string &s) { return std::any{std::stoi(s)}; });

    RegisterType<float>(
        "float", [](const std::any &v) { return std::to_string(std::any_cast<float>(v)); },
        [](const std::string &s) { return std::any{std::stof(s)}; });

    RegisterType<double>(
        "double", [](const std::any &v) { return std::to_string(std::any_cast<double>(v)); },
        [](const std::string &s) { return std::any{std::stod(s)}; });

    RegisterType<std::string>(
        "string", [](const std::any &v) { return std::any_cast<std::string>(v); },
        [](const std::string &s) { return std::any{s}; });

    RegisterType<bool>(
        "bool", [](const std::any &v) { return std::any_cast<bool>(v) ? "true" : "false"; },
        [](const std::string &s) { return std::any{s == "true"}; });

    RegisterType<size_t>(
        "size_t", [](const std::any &v) { return std::to_string(std::any_cast<size_t>(v)); },
        [](const std::string &s) { return std::any{static_cast<size_t>(std::stoull(s))}; });

    // Register ResourceType enum with proper type name
    RegisterType<ResourceType>(
        "enum infengine::ResourceType",
        [](const std::any &v) {
            ResourceType rt = std::any_cast<ResourceType>(v);
            switch (rt) {
            case ResourceType::Shader:
                return std::string("Shader");
            case ResourceType::Texture:
                return std::string("Texture");
            case ResourceType::Mesh:
                return std::string("Mesh");
            case ResourceType::Script:
                return std::string("Script");
            case ResourceType::DefaultText:
                return std::string("DefaultText");
            case ResourceType::DefaultBinary:
                return std::string("DefaultBinary");
            case ResourceType::Material:
                return std::string("Material");
            default:
                return std::string("Unknown");
            }
        },
        [](const std::string &s) -> std::any {
            if (s == "Shader")
                return std::any{ResourceType::Shader};
            if (s == "Texture")
                return std::any{ResourceType::Texture};
            if (s == "Mesh")
                return std::any{ResourceType::Mesh};
            if (s == "Script")
                return std::any{ResourceType::Script};
            if (s == "DefaultText")
                return std::any{ResourceType::DefaultText};
            if (s == "DefaultBinary")
                return std::any{ResourceType::DefaultBinary};
            if (s == "Material")
                return std::any{ResourceType::Material};
            if (s == "Unknown")
                return std::any{ResourceType::DefaultText};
            return std::any{ResourceType::DefaultText};
        });
}
} // namespace infengine