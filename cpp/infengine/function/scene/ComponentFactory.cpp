#include "ComponentFactory.h"
#include "Component.h"
#include <unordered_map>

namespace infengine
{
namespace
{
std::unordered_map<std::string, ComponentFactory::Creator> &GetRegistry()
{
    static std::unordered_map<std::string, ComponentFactory::Creator> registry;
    return registry;
}
} // namespace

bool ComponentFactory::Register(const std::string &typeName, Creator creator)
{
    auto &registry = GetRegistry();
    return registry.emplace(typeName, std::move(creator)).second;
}

std::unique_ptr<Component> ComponentFactory::Create(const std::string &typeName)
{
    auto &registry = GetRegistry();
    auto it = registry.find(typeName);
    if (it == registry.end())
        return nullptr;
    return it->second ? it->second() : nullptr;
}

bool ComponentFactory::IsRegistered(const std::string &typeName)
{
    auto &registry = GetRegistry();
    return registry.find(typeName) != registry.end();
}

std::vector<std::string> ComponentFactory::GetRegisteredTypeNames()
{
    auto &registry = GetRegistry();
    std::vector<std::string> names;
    names.reserve(registry.size());
    for (const auto &pair : registry)
        names.push_back(pair.first);
    return names;
}

} // namespace infengine
