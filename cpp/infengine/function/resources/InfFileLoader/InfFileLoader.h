#pragma once

#include "InfResource/InfResource.h"
#include "InfResource/InfResourceMeta.h"
#include <cstddef>
#include <string>
#include <vector>

namespace infengine
{

class InfFileLoader
{
  public:
    virtual ~InfFileLoader() = default;

    virtual bool LoadMeta(const char *content, const std::string &filePath, InfResourceMeta &metaData) = 0;
    virtual void CreateMeta(const char *content, size_t contentSize, const std::string &filePath,
                            InfResourceMeta &metaData) = 0;
    virtual std::unique_ptr<InfResource> Load(const char *content, size_t contentSize, InfResourceMeta &metaData) = 0;
};

} // namespace infengine