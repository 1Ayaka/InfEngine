#include "AudioEngine.h"
#include "AudioClip.h"
#include "AudioListener.h"
#include "AudioSource.h"
#include <core/log/InfLog.h>
#include <function/scene/GameObject.h>
#include <function/scene/Transform.h>

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace infengine
{

AudioEngine &AudioEngine::Instance()
{
    static AudioEngine instance;
    return instance;
}

AudioEngine::~AudioEngine()
{
    Shutdown();
}

bool AudioEngine::Initialize()
{
    if (m_initialized) {
        INFLOG_WARN("AudioEngine already initialized");
        return true;
    }

    // Initialize SDL audio subsystem (video may already be initialized)
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        INFLOG_ERROR("Failed to initialize SDL audio subsystem: ", SDL_GetError());
        return false;
    }

    // Open default playback device with reasonable defaults
    // SDL3 style: pass SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK and let SDL pick format
    SDL_AudioSpec requestedSpec = {};
    requestedSpec.format = SDL_AUDIO_F32; // 32-bit float for mixing quality
    requestedSpec.channels = 2;           // Stereo output
    requestedSpec.freq = 44100;           // 44.1 kHz

    m_deviceId = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &requestedSpec);
    if (m_deviceId == 0) {
        INFLOG_ERROR("Failed to open audio device: ", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    // Query actual device format
    SDL_AudioSpec actualSpec = {};
    int sampleFrames = 0;
    if (SDL_GetAudioDeviceFormat(m_deviceId, &actualSpec, &sampleFrames)) {
        m_deviceSpec = actualSpec;
        INFLOG_INFO("Audio device opened: ", m_deviceSpec.freq, " Hz, ", m_deviceSpec.channels,
                    " ch, format=", static_cast<int>(m_deviceSpec.format));
    } else {
        // Use requested spec as fallback
        m_deviceSpec = requestedSpec;
        INFLOG_WARN("Could not query device format, using requested spec");
    }

    // Make sure playback device is running (some backends open paused).
    if (!SDL_ResumeAudioDevice(m_deviceId)) {
        INFLOG_ERROR("Failed to resume audio device: ", SDL_GetError());
        SDL_CloseAudioDevice(m_deviceId);
        m_deviceId = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    m_initialized = true;
    INFLOG_INFO("AudioEngine initialized successfully");
    return true;
}

void AudioEngine::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    INFLOG_DEBUG("AudioEngine shutting down...");

    // Clear registered sources
    {
        std::lock_guard<std::mutex> lock(m_sourcesMutex);
        m_registeredSources.clear();
    }

    // Destroy all active streams
    {
        std::lock_guard<std::mutex> lock(m_streamsMutex);
        for (auto *stream : m_activeStreams) {
            if (stream) {
                SDL_DestroyAudioStream(stream);
            }
        }
        m_activeStreams.clear();
    }

    // Close the audio device
    if (m_deviceId != 0) {
        SDL_CloseAudioDevice(m_deviceId);
        m_deviceId = 0;
    }

    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    m_activeListener = nullptr;
    m_initialized = false;
    INFLOG_INFO("AudioEngine shut down");
}

// ============================================================================
// Spatial audio: per-frame 3D positioning
// ============================================================================

float AudioEngine::ComputeAttenuation(float distance, float minDist, float maxDist)
{
    if (maxDist <= minDist) {
        return distance <= minDist ? 1.0f : 0.0f;
    }
    if (distance <= minDist) {
        return 1.0f;
    }
    if (distance >= maxDist) {
        return 0.0f;
    }
    // Inverse-distance falloff, clamped to [0, 1]
    // Unity uses: minDist / distance  (for logarithmic)
    // We use linear for simplicity; can switch later.
    return 1.0f - (distance - minDist) / (maxDist - minDist);
}

void AudioEngine::Update(float /*deltaTime*/)
{
    if (!m_initialized) {
        return;
    }

    // Get listener world position and orientation
    glm::vec3 listenerPos(0.0f);
    glm::vec3 listenerRight(1.0f, 0.0f, 0.0f);
    bool hasListener = false;

    if (m_activeListener) {
        auto *listenerGO = m_activeListener->GetGameObject();
        if (listenerGO) {
            auto *listenerTr = listenerGO->GetTransform();
            if (listenerTr) {
                listenerPos = listenerTr->GetWorldPosition();
                listenerRight = listenerTr->GetRight();
                hasListener = true;
            }
        }
    }

    // Update spatialization for all registered sources
    std::lock_guard<std::mutex> lock(m_sourcesMutex);
    for (auto *source : m_registeredSources) {
        if (!source) {
            continue;
        }

        // Collect all active streams from this source's tracks
        auto streams = source->GetActiveStreams();
        if (streams.empty()) {
            continue;
        }

        // Compute spatial gain
        float spatialGain = 1.0f;

        if (hasListener) {
            auto *sourceGO = source->GetGameObject();
            if (sourceGO) {
                auto *sourceTr = sourceGO->GetTransform();
                if (sourceTr) {
                    glm::vec3 sourcePos = sourceTr->GetWorldPosition();
                    float distance = glm::length(sourcePos - listenerPos);
                    spatialGain = ComputeAttenuation(distance, source->GetMinDistance(), source->GetMaxDistance());

                    // Simple stereo panning based on listener's right vector
                    // pan ∈ [-1, 1]:  -1 = left,  0 = center,  1 = right
                    glm::vec3 toSource = (distance > 0.001f) ? (sourcePos - listenerPos) / distance : glm::vec3(0.0f);
                    float pan = glm::dot(toSource, listenerRight);

                    // Store computed pan on the source for per-stream application
                    source->SetComputedSpatialGain(spatialGain);
                    source->SetComputedPan(pan);
                }
            }
        } else {
            // No listener — play at full volume, center pan
            source->SetComputedSpatialGain(1.0f);
            source->SetComputedPan(0.0f);
        }

        // Apply final gain to each stream (source handles per-track volume)
        source->ApplyAllTrackGains();
    }
}

SDL_AudioStream *AudioEngine::CreateVoice(AudioSource * /*source*/, AudioClip *clip)
{
    if (!m_initialized || !clip || !clip->IsLoaded()) {
        return nullptr;
    }

    // Create a stream that converts from the clip's format to the device format
    SDL_AudioSpec srcSpec = {};
    srcSpec.format = clip->GetFormat();
    srcSpec.channels = clip->GetChannels();
    srcSpec.freq = clip->GetSampleRate();

    SDL_AudioStream *stream = SDL_CreateAudioStream(&srcSpec, &m_deviceSpec);
    if (!stream) {
        INFLOG_ERROR("Failed to create audio stream: ", SDL_GetError());
        return nullptr;
    }

    // Push the entire clip data into the stream
    const auto &data = clip->GetData();
    if (!SDL_PutAudioStreamData(stream, data.data(), static_cast<int>(data.size()))) {
        INFLOG_ERROR("Failed to put audio data into stream: ", SDL_GetError());
        SDL_DestroyAudioStream(stream);
        return nullptr;
    }

    // Bind the stream to our playback device
    if (!SDL_BindAudioStream(m_deviceId, stream)) {
        INFLOG_ERROR("Failed to bind audio stream to device: ", SDL_GetError());
        SDL_DestroyAudioStream(stream);
        return nullptr;
    }

    // Track the stream
    {
        std::lock_guard<std::mutex> lock(m_streamsMutex);
        m_activeStreams.push_back(stream);
    }

    return stream;
}

void AudioEngine::DestroyVoice(SDL_AudioStream *stream)
{
    if (!stream) {
        return;
    }

    SDL_UnbindAudioStream(stream);
    SDL_DestroyAudioStream(stream);

    // Remove from tracking
    {
        std::lock_guard<std::mutex> lock(m_streamsMutex);
        m_activeStreams.erase(std::remove(m_activeStreams.begin(), m_activeStreams.end(), stream),
                              m_activeStreams.end());
    }
}

// ============================================================================
// Source registration
// ============================================================================

void AudioEngine::RegisterSource(AudioSource *source)
{
    if (!source) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_sourcesMutex);
    m_registeredSources.insert(source);
}

void AudioEngine::UnregisterSource(AudioSource *source)
{
    if (!source) {
        return;
    }
    std::lock_guard<std::mutex> lock(m_sourcesMutex);
    m_registeredSources.erase(source);
}

void AudioEngine::SetActiveListener(AudioListener *listener)
{
    m_activeListener = listener;
}

void AudioEngine::SetMasterVolume(float volume)
{
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
    if (m_deviceId != 0) {
        SDL_SetAudioDeviceGain(m_deviceId, m_masterVolume);
    }
}

void AudioEngine::PauseAll()
{
    if (m_deviceId != 0) {
        SDL_PauseAudioDevice(m_deviceId);
        m_globalPaused = true;
    }
}

void AudioEngine::ResumeAll()
{
    if (m_deviceId != 0) {
        SDL_ResumeAudioDevice(m_deviceId);
        m_globalPaused = false;
    }
}

} // namespace infengine
