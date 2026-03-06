/**
 * @file FrameDeletionQueue.h
 * @brief Per-frame deferred deletion queue for Vulkan resources
 *
 * Vulkan resources (buffers, images, etc.) may still be referenced by
 * in-flight command buffers when the CPU side decides to destroy them.
 * This queue defers actual destruction until `maxFramesInFlight` frames
 * have elapsed, guaranteeing that every in-flight command buffer has
 * finished executing by the time the resources are freed.
 *
 * Usage:
 *   // At init:
 *   queue.Initialize(maxFramesInFlight);
 *
 *   // When a resource should be deleted:
 *   queue.Push([buffer = std::move(myBuffer)]() mutable { buffer.reset(); });
 *
 *   // Once per frame, AFTER fence wait:
 *   queue.Tick();
 *
 *   // At shutdown:
 *   queue.FlushAll();
 */

#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace infengine
{

class FrameDeletionQueue
{
  public:
    FrameDeletionQueue() = default;
    ~FrameDeletionQueue()
    {
        FlushAll();
    }

    // Non-copyable, movable
    FrameDeletionQueue(const FrameDeletionQueue &) = delete;
    FrameDeletionQueue &operator=(const FrameDeletionQueue &) = delete;
    FrameDeletionQueue(FrameDeletionQueue &&) = default;
    FrameDeletionQueue &operator=(FrameDeletionQueue &&) = default;

    /// @brief Initialize with the number of frames that may be in-flight
    void Initialize(uint32_t maxFramesInFlight)
    {
        m_maxFramesInFlight = maxFramesInFlight;
    }

    /// @brief Queue a cleanup lambda for deferred execution.
    ///
    /// The lambda will be invoked after at least @c maxFramesInFlight
    /// calls to @c Tick(), ensuring all in-flight command buffers
    /// referencing the resource have completed.
    void Push(std::function<void()> deleter)
    {
        m_entries.push_back({m_frameCounter, std::move(deleter)});
    }

    /// @brief Call exactly once per frame, AFTER the per-frame fence wait.
    ///
    /// Flushes entries whose frame age >= maxFramesInFlight, then
    /// increments the internal frame counter.
    void Tick()
    {
        // Partition: move deletable entries to the front, then erase.
        // Use a simple scan to avoid allocation from std::partition.
        size_t writeIdx = 0;
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (m_frameCounter - m_entries[i].frameNumber >= m_maxFramesInFlight) {
                // Safe to destroy now — all in-flight frames have cycled.
                m_entries[i].deleter();
            } else {
                // Keep this entry — still potentially in-flight.
                if (writeIdx != i) {
                    m_entries[writeIdx] = std::move(m_entries[i]);
                }
                ++writeIdx;
            }
        }
        m_entries.resize(writeIdx);
        ++m_frameCounter;
    }

    /// @brief Immediately flush ALL remaining entries (use at shutdown).
    void FlushAll()
    {
        for (auto &entry : m_entries) {
            entry.deleter();
        }
        m_entries.clear();
    }

    /// @brief Get the number of pending entries
    [[nodiscard]] size_t PendingCount() const
    {
        return m_entries.size();
    }

  private:
    struct Entry
    {
        uint64_t frameNumber = 0;
        std::function<void()> deleter;
    };

    std::vector<Entry> m_entries;
    uint64_t m_frameCounter = 0;
    uint32_t m_maxFramesInFlight = 2;
};

} // namespace infengine
