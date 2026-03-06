#pragma once

#include <SDL3/SDL_audio.h>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace infengine
{

// Forward declarations
class AudioClip;
class AudioSource;
class AudioListener;

/**
 * @brief Core audio engine managing SDL3 audio device and stream mixing.
 *
 * Singleton that owns the SDL3 audio device, manages active voices
 * (AudioStreams), and performs per-frame mixing with 3D spatialization.
 *
 * Design:
 * - One logical SDL3 audio device for all playback
 * - Each playing AudioSource owns SDL_AudioStream voices bound to the device
 * - Per-frame Update() adjusts stream gain/panning for 3D positioning
 * - Thread-safe: SDL3 audio runs its own mixing thread; we only push data
 */
class AudioEngine
{
  public:
    /// @brief Get the singleton instance
    static AudioEngine &Instance();

    /// @brief Initialize the audio subsystem (call once at engine startup)
    /// @return true on success
    bool Initialize();

    /// @brief Shutdown the audio subsystem (call at engine cleanup)
    void Shutdown();

    /// @brief Whether the audio engine is initialized
    [[nodiscard]] bool IsInitialized() const
    {
        return m_initialized;
    }

    /// @brief Per-frame update: recalculate 3D spatialization for all active sources
    /// @param deltaTime Time since last frame
    void Update(float deltaTime);

    // ========================================================================
    // Voice management (called by AudioSource)
    // ========================================================================

    /// @brief Create an SDL_AudioStream for a source and bind it to the device
    /// @param source The AudioSource requesting playback
    /// @param clip The AudioClip to play
    /// @return Stream handle, or nullptr on failure
    SDL_AudioStream *CreateVoice(AudioSource *source, AudioClip *clip);

    /// @brief Destroy a voice stream and unbind it from the device
    /// @param stream The stream to destroy
    void DestroyVoice(SDL_AudioStream *stream);

    // ========================================================================
    // Source registration (for spatial audio)
    // ========================================================================

    /// @brief Register an AudioSource for per-frame spatial updates
    void RegisterSource(AudioSource *source);

    /// @brief Unregister an AudioSource (called on disable/destroy)
    void UnregisterSource(AudioSource *source);

    // ========================================================================
    // Listener management
    // ========================================================================

    /// @brief Register an AudioListener as active
    void SetActiveListener(AudioListener *listener);

    /// @brief Get the currently active listener
    [[nodiscard]] AudioListener *GetActiveListener() const
    {
        return m_activeListener;
    }

    // ========================================================================
    // Global audio settings
    // ========================================================================

    /// @brief Set master volume (0.0 = silence, 1.0 = full)
    void SetMasterVolume(float volume);

    /// @brief Get master volume
    [[nodiscard]] float GetMasterVolume() const
    {
        return m_masterVolume;
    }

    /// @brief Pause all audio playback
    void PauseAll();

    /// @brief Resume all audio playback
    void ResumeAll();

    /// @brief Whether all audio is paused
    [[nodiscard]] bool IsPaused() const
    {
        return m_globalPaused;
    }

    /// @brief Get the output sample rate
    [[nodiscard]] int GetSampleRate() const
    {
        return m_deviceSpec.freq;
    }

    /// @brief Get the output channel count
    [[nodiscard]] int GetChannelCount() const
    {
        return m_deviceSpec.channels;
    }

    ~AudioEngine();

  private:
    AudioEngine() = default;

    AudioEngine(const AudioEngine &) = delete;
    AudioEngine &operator=(const AudioEngine &) = delete;

    /// @brief Compute distance-based attenuation (inverse-distance clamped)
    static float ComputeAttenuation(float distance, float minDist, float maxDist);

    bool m_initialized = false;
    bool m_globalPaused = false;
    float m_masterVolume = 1.0f;

    SDL_AudioDeviceID m_deviceId = 0;
    SDL_AudioSpec m_deviceSpec = {};

    AudioListener *m_activeListener = nullptr;

    /// Active voice streams (for tracking and cleanup)
    std::vector<SDL_AudioStream *> m_activeStreams;
    std::mutex m_streamsMutex;

    /// Registered AudioSources for spatial updates
    std::unordered_set<AudioSource *> m_registeredSources;
    std::mutex m_sourcesMutex;
};

} // namespace infengine
