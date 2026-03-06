#pragma once
#include <core/types/InfFwdType.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace infengine
{
// ----------------------------------
// Resource Type Mapping Templates
// ----------------------------------

// Forward declaration for InfTextureData
struct InfTextureData;

/// @brief GPU texture handle containing Vulkan resources
struct GPUTextureHandle
{
    void *image = nullptr;         ///< VkImage
    void *imageView = nullptr;     ///< VkImageView
    void *sampler = nullptr;       ///< VkSampler
    void *descriptorSet = nullptr; ///< ImGui descriptor for GUI display
    std::string name;              ///< Texture name/identifier
    int width = 0;
    int height = 0;
    bool isValid = false;
};

template <ResourceType T> struct ResourceTypeMapping;

template <> struct ResourceTypeMapping<ResourceType::Shader>
{
    using Raw = std::vector<char>;
    using Compiled = std::vector<char>;
};

template <> struct ResourceTypeMapping<ResourceType::Texture>
{
    using Raw = InfTextureData;        ///< CPU-side pixel data from stb_image
    using Compiled = GPUTextureHandle; ///< GPU-side Vulkan texture resources
};

// Forward declaration for InfMaterial
class InfMaterial;

template <> struct ResourceTypeMapping<ResourceType::Material>
{
    using Raw = std::shared_ptr<InfMaterial>;      ///< Material definition
    using Compiled = std::shared_ptr<InfMaterial>; ///< Same for now (pipeline created later)
};

// Forward declaration for AudioClip
class AudioClip;

template <> struct ResourceTypeMapping<ResourceType::Audio>
{
    using Raw = std::vector<char>;               ///< Raw file bytes
    using Compiled = std::shared_ptr<AudioClip>; ///< Decoded audio clip
};

// ----------------------------------
// InfResource Class
// ----------------------------------

class InfResource
{
  public:
    // Constructor
    InfResource(ResourceType type, std::shared_ptr<void> rawData, std::shared_ptr<void> compiledData);

    // Destructor
    ~InfResource() = default;

    // Copy constructor and assignment operator
    InfResource(const InfResource &other) = default;
    InfResource &operator=(const InfResource &other) = default;

    // Move constructor and assignment operator
    InfResource(InfResource &&other) noexcept = default;
    InfResource &operator=(InfResource &&other) noexcept = default;

    // Getters
    ResourceType GetType() const;
    std::shared_ptr<void> GetRawData() const;
    std::shared_ptr<void> GetCompiledData() const;

  private:
    ResourceType m_type;
    /// CPU-side raw data (e.g. InfTextureData pixel buffer, shader SPIR-V bytes).
    /// Accessed via GetResRaw<T>(). May be nullptr for GPU-only resources once
    /// the compiled data is uploaded and the CPU copy is no longer needed.
    std::shared_ptr<void> m_rawData;
    std::shared_ptr<void> m_compiledData;
};

// ----------------------------------
// Resource Factory Functions
// ----------------------------------

template <ResourceType T>
std::unique_ptr<InfResource> MakeResource(typename ResourceTypeMapping<T>::Raw *FNraw,
                                          typename ResourceTypeMapping<T>::Compiled *FNcompiled);

// ----------------------------------
// Resource Access Functions
// ----------------------------------

template <ResourceType T> typename ResourceTypeMapping<T>::Raw *GetResRaw(const InfResource &res);

template <ResourceType T> typename ResourceTypeMapping<T>::Compiled *GetResCompiled(const InfResource &res);

} // namespace infengine

// Include template implementations
#include "InfResource.inl"
