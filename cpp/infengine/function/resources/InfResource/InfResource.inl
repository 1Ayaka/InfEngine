#pragma once

#include <cassert>

namespace infengine
{

// ----------------------------------
// Template Function Implementations
// ----------------------------------

template <ResourceType T>
std::unique_ptr<InfResource> MakeResource(
    typename ResourceTypeMapping<T>::Raw* FNraw,
    typename ResourceTypeMapping<T>::Compiled* FNcompiled)
{
    auto rawDeleter = [](void* p) { 
        delete static_cast<typename ResourceTypeMapping<T>::Raw*>(p); 
    };
    
    auto compiledDeleter = [](void* p) { 
        delete static_cast<typename ResourceTypeMapping<T>::Compiled*>(p); 
    };
    
    return std::make_unique<InfResource>(
        T,
        std::shared_ptr<void>(FNraw, rawDeleter),
        std::shared_ptr<void>(FNcompiled, compiledDeleter)
    );
}

template <ResourceType T> 
typename ResourceTypeMapping<T>::Raw* GetResRaw(const InfResource& res)
{
    assert(res.GetType() == T && "Type mismatch: wrong access to raw data");
    return static_cast<typename ResourceTypeMapping<T>::Raw*>(res.GetRawData().get());
}

template <ResourceType T> 
typename ResourceTypeMapping<T>::Compiled* GetResCompiled(const InfResource& res)
{
    assert(res.GetType() == T && "Type mismatch: wrong access to compiled data");
    return static_cast<typename ResourceTypeMapping<T>::Compiled*>(res.GetCompiledData().get());
}

} // namespace infengine
