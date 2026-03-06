#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace infengine
{

/**
 * @brief Key for identifying a compiled shader variant
 */
struct ShaderCacheKey
{
    std::string shaderPath;                   // Path to shader source
    std::vector<std::string> enabledKeywords; // Sorted list of enabled keywords/defines
    uint64_t sourceHash = 0;                  // Hash of shader source content

    bool operator==(const ShaderCacheKey &other) const
    {
        return shaderPath == other.shaderPath && enabledKeywords == other.enabledKeywords &&
               sourceHash == other.sourceHash;
    }

    [[nodiscard]] size_t Hash() const
    {
        size_t hash = std::hash<std::string>{}(shaderPath);
        hash ^= sourceHash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        for (const auto &keyword : enabledKeywords) {
            hash ^= std::hash<std::string>{}(keyword) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        }
        return hash;
    }
};

/**
 * @brief Cached compiled SPIR-V data
 */
struct CachedSpirv
{
    std::vector<uint32_t> spirvData;
    uint64_t sourceHash = 0;       // Hash of source when compiled
    uint64_t compileTimestamp = 0; // When it was compiled
    bool valid = false;
};

/**
 * @brief ShaderCache - Persistent disk cache for compiled SPIR-V
 *
 * Features:
 * - Disk persistence to avoid recompilation on restart
 * - Hash-based invalidation when source changes
 * - Support for shader variants (different keyword combinations)
 * - Thread-safe access
 */
class ShaderCache
{
  public:
    ShaderCache() = default;
    ~ShaderCache() = default;

    // Non-copyable
    ShaderCache(const ShaderCache &) = delete;
    ShaderCache &operator=(const ShaderCache &) = delete;

    /// @brief Initialize cache with cache directory path
    void Initialize(const std::string &cacheDirectory);

    /// @brief Shutdown and save any pending cache entries
    void Shutdown();

    /// @brief Check if we have a valid cached version of this shader variant
    [[nodiscard]] bool HasCached(const ShaderCacheKey &key) const;

    /// @brief Get cached SPIR-V if available and valid
    [[nodiscard]] CachedSpirv GetCached(const ShaderCacheKey &key) const;

    /// @brief Store compiled SPIR-V in cache
    void Store(const ShaderCacheKey &key, const std::vector<uint32_t> &spirv);

    /// @brief Clear all cached data
    void ClearCache();

    /// @brief Get cache directory path
    [[nodiscard]] const std::string &GetCacheDirectory() const
    {
        return m_cacheDirectory;
    }

    /// @brief Compute hash of shader source content
    [[nodiscard]] static uint64_t ComputeSourceHash(const std::string &source);

    /// @brief Compute hash of shader source content with defines prepended
    [[nodiscard]] static uint64_t ComputeSourceHashWithDefines(const std::string &source,
                                                               const std::vector<std::string> &defines);

    /// @brief Get number of cached entries
    [[nodiscard]] size_t GetCacheSize() const;

    /// @brief Precompile all shaders in a directory (for build-time compilation)
    void PrecompileShaders(
        const std::string &shaderDirectory,
        std::function<std::vector<uint32_t>(const std::string &, const std::vector<std::string> &)> compileFunction);

  private:
    /// @brief Generate cache file path for a shader key
    [[nodiscard]] std::filesystem::path GetCacheFilePath(const ShaderCacheKey &key) const;

    /// @brief Load a single cache entry from disk
    [[nodiscard]] CachedSpirv LoadFromDisk(const std::filesystem::path &path) const;

    /// @brief Save a single cache entry to disk
    void SaveToDisk(const std::filesystem::path &path, const CachedSpirv &cached) const;

    /// @brief Load all cached entries from disk on startup
    void LoadCacheIndex();

    /// @brief Save cache index to disk
    void SaveCacheIndex();

    std::string m_cacheDirectory;
    mutable std::mutex m_mutex;
    std::unordered_map<size_t, CachedSpirv> m_cache; // key hash -> cached data
    bool m_initialized = false;
};

} // namespace infengine

// Hash function for ShaderCacheKey
namespace std
{
template <> struct hash<infengine::ShaderCacheKey>
{
    size_t operator()(const infengine::ShaderCacheKey &key) const
    {
        return key.Hash();
    }
};
} // namespace std
