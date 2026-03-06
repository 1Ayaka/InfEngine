/**
 * @file VmaContext.h
 * @brief Vulkan Memory Allocator (VMA) integration
 *
 * Provides initialization and access to the global VmaAllocator instance.
 * VMA replaces all manual vkAllocateMemory/vkFreeMemory calls with
 * suballocated, pooled memory management that avoids the driver's
 * per-process allocation limit (typically 4096 on Windows).
 *
 * AMD/Intel considerations:
 * - AMD discrete: VMA uses DEVICE_LOCAL heap for GPU-only resources
 * - Intel UMA/integrated: VMA's AUTO usage picks the optimal shared heap
 * - VMA_MEMORY_USAGE_AUTO handles both correctly
 */

#pragma once

#include <vk_mem_alloc.h>

namespace infengine
{
namespace vk
{

/**
 * @brief Create a VmaAllocator for the given Vulkan instance/device.
 *
 * Call once after VkDevice creation. The returned allocator must be
 * destroyed with DestroyVmaAllocator() before destroying the VkDevice.
 *
 * @param instance  Vulkan instance handle
 * @param physicalDevice Physical device handle
 * @param device    Logical device handle
 * @return Valid VmaAllocator, or VK_NULL_HANDLE on failure
 */
[[nodiscard]] VmaAllocator CreateVmaAllocator(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);

/**
 * @brief Destroy a VmaAllocator.
 *
 * All VMA allocations must be freed before calling this.
 *
 * @param allocator The allocator to destroy (may be VK_NULL_HANDLE)
 */
void DestroyVmaAllocator(VmaAllocator allocator);

} // namespace vk
} // namespace infengine
