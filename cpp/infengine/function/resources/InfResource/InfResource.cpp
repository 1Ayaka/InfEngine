#include "InfResource.h"

namespace infengine
{

// ----------------------------------
// InfResource Implementation
// ----------------------------------

InfResource::InfResource(ResourceType type, std::shared_ptr<void> rawData, std::shared_ptr<void> compiledData)
    : m_type(type), m_rawData(std::move(rawData)), m_compiledData(std::move(compiledData))
{
}

ResourceType InfResource::GetType() const
{
    return m_type;
}

std::shared_ptr<void> InfResource::GetRawData() const
{
    return m_rawData;
}

std::shared_ptr<void> InfResource::GetCompiledData() const
{
    return m_compiledData;
}

} // namespace infengine
