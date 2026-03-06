#pragma once

#include <core/log/InfLog.h>

namespace infengine
{

// ----------------------------------
// Template Function Implementations
// ----------------------------------

template <typename T> 
T InfResourceMeta::GetDataAs(const std::string& key) const
{
    auto it = m_metadata.find(key);
    if (it != m_metadata.end()) {
        const auto& metaType = it->second;
        if (metaType.first == InfTypeRegistry::GetInstance().GetTypeName(typeid(T))) {
            return std::any_cast<T>(metaType.second);
        }
        INFLOG_ERROR("Metadata type mismatch for key: ", key, 
                     ", expected: ", InfTypeRegistry::GetInstance().GetTypeName(typeid(T)),
                     ", got: ", metaType.first);
    } else {
        INFLOG_ERROR("Metadata not found for key: ", key);
    }
    return T{}; // Return default-constructed value
}

} // namespace infengine
