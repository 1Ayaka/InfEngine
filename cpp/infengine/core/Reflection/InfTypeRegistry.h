#pragma once

#include <any>
#include <functional>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>

namespace infengine
{
class InfTypeRegistry
{
    using Factory = std::function<std::any()>;
    using ToStringFunc = std::function<std::string(const std::any &)>;
    using FromStringFunc = std::function<std::any(const std::string &)>;

  public:
    static InfTypeRegistry &GetInstance();

    template <typename T> void RegisterType(const std::string &typeName, ToStringFunc toStr, FromStringFunc fromStr)
    {
        m_typeConstructors[typeName] = [] { return T{}; };
        m_typeNames[std::type_index(typeid(T))] = typeName;
        m_toStringFuncs[typeName] = std::move(toStr);
        m_fromStringFuncs[typeName] = std::move(fromStr);
    }

    std::any Create(const std::string &typeName) const;
    std::string GetTypeName(std::type_index typeindex) const;
    std::string ToString(const std::string &typeName, const std::any &v) const;
    std::any FromString(const std::string &typeName, const std::string &str) const;

  private:
    InfTypeRegistry();

    void Build();

    std::unordered_map<std::string, Factory> m_typeConstructors;
    std::unordered_map<std::type_index, std::string> m_typeNames;
    std::unordered_map<std::string, ToStringFunc> m_toStringFuncs;
    std::unordered_map<std::string, FromStringFunc> m_fromStringFuncs;
};
} // namespace infengine