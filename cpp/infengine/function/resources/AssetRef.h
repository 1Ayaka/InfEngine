#pragma once

#include <core/types/InfFwdType.h>

#include <functional>
#include <memory>
#include <string>

namespace infengine
{

/**
 * @brief A lightweight, serializable reference to an asset identified by GUID.
 *
 * AssetRef<T> stores only a GUID string and an optional cached pointer.
 * Resolution is explicit — call Resolve() with a resolver callback or
 * set the cached pointer directly after loading.
 *
 * Serialization: serialize/deserialize the GUID string via GetGuid()/SetGuid().
 *
 * @tparam T The asset type (e.g. InfMaterial, InfTextureData, etc.)
 */
template <typename T> class AssetRef
{
  public:
    AssetRef() = default;

    /// @brief Construct with a GUID
    explicit AssetRef(const std::string &guid) : m_guid(guid)
    {
    }

    /// @brief Construct with GUID and a pre-resolved pointer
    AssetRef(const std::string &guid, std::shared_ptr<T> asset) : m_guid(guid), m_cached(std::move(asset))
    {
    }

    // ---- GUID accessors ----

    [[nodiscard]] const std::string &GetGuid() const
    {
        return m_guid;
    }

    void SetGuid(const std::string &guid)
    {
        if (m_guid != guid) {
            m_guid = guid;
            m_cached.reset(); // invalidate cache on GUID change
        }
    }

    /// @brief Check whether a GUID is assigned (may or may not be resolved)
    [[nodiscard]] bool HasGuid() const
    {
        return !m_guid.empty();
    }

    // ---- Cache accessors ----

    /// @brief Get the cached asset pointer (may be nullptr if not yet resolved)
    [[nodiscard]] std::shared_ptr<T> Get() const
    {
        return m_cached;
    }

    /// @brief Arrow operator for convenient access (caller must ensure resolved)
    T *operator->() const
    {
        return m_cached.get();
    }

    /// @brief Dereference (caller must ensure resolved)
    T &operator*() const
    {
        return *m_cached;
    }

    /// @brief Bool conversion — true if resolved and non-null
    explicit operator bool() const
    {
        return m_cached != nullptr;
    }

    /// @brief Set the cached pointer directly (e.g. after loading)
    void SetCached(std::shared_ptr<T> asset)
    {
        m_cached = std::move(asset);
    }

    /// @brief Invalidate the cached pointer without clearing the GUID
    void Invalidate()
    {
        m_cached.reset();
    }

    // ---- Resolution ----

    /// @brief Resolve the reference using a resolver callback.
    /// The callback receives the GUID and should return a shared_ptr<T>.
    /// Returns true if resolve succeeded (non-null result).
    using ResolverFn = std::function<std::shared_ptr<T>(const std::string &guid)>;

    bool Resolve(const ResolverFn &resolver)
    {
        if (m_guid.empty())
            return false;
        m_cached = resolver(m_guid);
        return m_cached != nullptr;
    }

    /// @brief Resolve only if not already cached
    bool ResolveIfNeeded(const ResolverFn &resolver)
    {
        if (m_cached)
            return true;
        return Resolve(resolver);
    }

    // ---- Equality ----

    bool operator==(const AssetRef &other) const
    {
        return m_guid == other.m_guid;
    }

    bool operator!=(const AssetRef &other) const
    {
        return m_guid != other.m_guid;
    }

  private:
    std::string m_guid;
    std::shared_ptr<T> m_cached;
};

} // namespace infengine
