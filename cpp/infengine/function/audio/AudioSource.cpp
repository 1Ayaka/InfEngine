#include "AudioSource.h"
#include "AudioEngine.h"
#include <core/log/InfLog.h>
#include <function/scene/ComponentFactory.h>
#include <function/scene/GameObject.h>

#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace infengine
{

// Register AudioSource with ComponentFactory so it can be created by type name
INFENGINE_REGISTER_COMPONENT("AudioSource", AudioSource)

AudioSource::AudioSource()
{
    // Default: 1 track
    m_tracks.resize(1);
}

AudioSource::~AudioSource()
{
    StopAll();
    AudioEngine::Instance().UnregisterSource(this);
}

void AudioSource::Awake()
{
    // Register with AudioEngine for spatial updates
    AudioEngine::Instance().RegisterSource(this);
}

void AudioSource::Start()
{
    if (m_playOnAwake && !m_tracks.empty() && m_tracks[0].clip && m_tracks[0].clip->IsLoaded()) {
        Play(0);
    }
}

void AudioSource::OnEnable()
{
    AudioEngine::Instance().RegisterSource(this);
}

void AudioSource::OnDisable()
{
    // Pause all playing tracks when component is disabled
    for (int i = 0; i < static_cast<int>(m_tracks.size()); ++i) {
        if (m_tracks[i].isPlaying && !m_tracks[i].isPaused) {
            Pause(i);
        }
    }
    AudioEngine::Instance().UnregisterSource(this);
}

void AudioSource::OnDestroy()
{
    StopAll();
    AudioEngine::Instance().UnregisterSource(this);
}

void AudioSource::Update(float /*deltaTime*/)
{
    for (int i = 0; i < static_cast<int>(m_tracks.size()); ++i) {
        auto &track = m_tracks[i];
        if (!track.isPlaying || track.isPaused || !track.stream) {
            continue;
        }

        // Check if stream has finished playing (non-looping)
        int available = SDL_GetAudioStreamAvailable(track.stream);
        if (available <= 0) {
            if (m_loop) {
                CheckLooping(i);
            } else {
                // Playback finished
                StopVoice(i);
            }
        }
    }
}

// ============================================================================
// Serialization
// ============================================================================

std::string AudioSource::Serialize() const
{
    json j = json::parse(Component::Serialize());
    j["volume"] = m_volume;
    j["pitch"] = m_pitch;
    j["loop"] = m_loop;
    j["play_on_awake"] = m_playOnAwake;
    j["mute"] = m_mute;
    j["min_distance"] = m_minDistance;
    j["max_distance"] = m_maxDistance;
    j["output_bus"] = m_outputBus;
    j["track_count"] = static_cast<int>(m_tracks.size());

    // Serialize per-track data
    json tracksJson = json::array();
    for (const auto &track : m_tracks) {
        json tj;
        tj["volume"] = track.volume;
        if (track.clip) {
            tj["clip_path"] = track.clip->GetFilePath();
        }
        tracksJson.push_back(tj);
    }
    j["tracks"] = tracksJson;

    // Backward compat: also write track 0 clip_path at root level
    if (!m_tracks.empty() && m_tracks[0].clip) {
        j["clip_path"] = m_tracks[0].clip->GetFilePath();
    }
    return j.dump(2);
}

bool AudioSource::Deserialize(const std::string &jsonStr)
{
    if (!Component::Deserialize(jsonStr)) {
        return false;
    }

    try {
        json j = json::parse(jsonStr);
        if (j.contains("volume"))
            m_volume = j["volume"].get<float>();
        if (j.contains("pitch"))
            m_pitch = j["pitch"].get<float>();
        if (j.contains("loop"))
            m_loop = j["loop"].get<bool>();
        if (j.contains("play_on_awake"))
            m_playOnAwake = j["play_on_awake"].get<bool>();
        if (j.contains("mute"))
            m_mute = j["mute"].get<bool>();
        if (j.contains("min_distance"))
            m_minDistance = j["min_distance"].get<float>();
        if (j.contains("max_distance"))
            m_maxDistance = j["max_distance"].get<float>();
        if (j.contains("output_bus"))
            m_outputBus = j["output_bus"].get<std::string>();

        // Deserialize tracks
        if (j.contains("tracks") && j["tracks"].is_array()) {
            int trackCount = j.contains("track_count") ? j["track_count"].get<int>() : 1;
            trackCount = std::max(1, trackCount);
            m_tracks.resize(trackCount);

            const auto &tracksJson = j["tracks"];
            for (int i = 0; i < std::min(static_cast<int>(tracksJson.size()), trackCount); ++i) {
                const auto &tj = tracksJson[i];
                if (tj.contains("volume"))
                    m_tracks[i].volume = tj["volume"].get<float>();
                m_tracks[i].clip.reset();
                if (tj.contains("clip_path")) {
                    const std::string clipPath = tj["clip_path"].get<std::string>();
                    if (!clipPath.empty()) {
                        auto clip = std::make_shared<AudioClip>();
                        if (clip->LoadFromFile(clipPath)) {
                            m_tracks[i].clip = std::move(clip);
                        } else {
                            INFLOG_WARN("AudioSource::Deserialize: failed to load clip for track ", i, ": ", clipPath);
                        }
                    }
                }
            }
        } else if (j.contains("clip_path")) {
            // Legacy: single clip_path at root → track 0
            m_tracks.resize(std::max(static_cast<int>(m_tracks.size()), 1));
            const std::string clipPath = j["clip_path"].get<std::string>();
            if (!clipPath.empty()) {
                auto clip = std::make_shared<AudioClip>();
                if (clip->LoadFromFile(clipPath)) {
                    m_tracks[0].clip = std::move(clip);
                } else {
                    INFLOG_WARN("AudioSource::Deserialize: failed to load clip_path: ", clipPath);
                }
            }
        }

        return true;
    } catch (const std::exception &e) {
        INFLOG_WARN("AudioSource::Deserialize failed: ", e.what());
        return false;
    }
}

// ============================================================================
// Track management
// ============================================================================

void AudioSource::SetTrackCount(int count)
{
    count = std::max(1, count);
    int oldCount = static_cast<int>(m_tracks.size());

    // Stop voices for tracks that are being removed
    for (int i = count; i < oldCount; ++i) {
        StopVoice(i);
    }

    m_tracks.resize(count);
}

void AudioSource::SetTrackClip(int trackIndex, std::shared_ptr<AudioClip> clip)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) {
        INFLOG_WARN("AudioSource::SetTrackClip: track index ", trackIndex, " out of range");
        return;
    }

    // Stop current playback on this track
    if (m_tracks[trackIndex].isPlaying) {
        StopVoice(trackIndex);
    }
    m_tracks[trackIndex].clip = std::move(clip);
}

std::shared_ptr<AudioClip> AudioSource::GetTrackClip(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) {
        return nullptr;
    }
    return m_tracks[trackIndex].clip;
}

void AudioSource::SetTrackVolume(int trackIndex, float volume)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) {
        return;
    }
    m_tracks[trackIndex].volume = std::clamp(volume, 0.0f, 1.0f);
    ApplyTrackGain(trackIndex);
}

float AudioSource::GetTrackVolume(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) {
        return 0.0f;
    }
    return m_tracks[trackIndex].volume;
}

// ============================================================================
// Playback control
// ============================================================================

void AudioSource::Play(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) {
        INFLOG_WARN("AudioSource::Play: track index ", trackIndex, " out of range");
        return;
    }

    auto &track = m_tracks[trackIndex];
    if (!track.clip || !track.clip->IsLoaded()) {
        INFLOG_WARN("AudioSource::Play: no clip loaded on track ", trackIndex);
        return;
    }

    // Stop any existing playback on this track
    StopVoice(trackIndex);

    // Start new voice
    StartVoice(trackIndex);
}

void AudioSource::Stop(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) {
        return;
    }
    StopVoice(trackIndex);
}

void AudioSource::Pause(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) {
        return;
    }
    auto &track = m_tracks[trackIndex];
    if (track.isPlaying && !track.isPaused && track.stream) {
        track.isPaused = true;
        // Set gain to 0 to effectively pause
        SDL_SetAudioStreamGain(track.stream, 0.0f);
    }
}

void AudioSource::UnPause(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) {
        return;
    }
    auto &track = m_tracks[trackIndex];
    if (track.isPlaying && track.isPaused && track.stream) {
        track.isPaused = false;
        ApplyTrackGain(trackIndex);
    }
}

void AudioSource::StopAll()
{
    for (int i = 0; i < static_cast<int>(m_tracks.size()); ++i) {
        StopVoice(i);
    }
}

bool AudioSource::IsTrackPlaying(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) {
        return false;
    }
    return m_tracks[trackIndex].isPlaying && !m_tracks[trackIndex].isPaused;
}

bool AudioSource::IsTrackPaused(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) {
        return false;
    }
    return m_tracks[trackIndex].isPaused;
}

// ============================================================================
// Source-level properties
// ============================================================================

void AudioSource::SetVolume(float volume)
{
    m_volume = std::clamp(volume, 0.0f, 1.0f);
    ApplyAllTrackGains();
}

void AudioSource::SetPitch(float pitch)
{
    m_pitch = std::clamp(pitch, 0.1f, 3.0f);
    // SDL3 doesn't have built-in pitch shifting on streams.
    // Future: implement via resampling rate adjustment.
}

void AudioSource::SetMute(bool mute)
{
    m_mute = mute;
    ApplyAllTrackGains();
}

uint64_t AudioSource::GetGameObjectId() const
{
    auto *go = GetGameObject();
    return go ? go->GetID() : 0;
}

// ============================================================================
// Spatial audio helpers
// ============================================================================

std::vector<SDL_AudioStream *> AudioSource::GetActiveStreams() const
{
    std::vector<SDL_AudioStream *> streams;
    for (const auto &track : m_tracks) {
        if (track.stream && track.isPlaying) {
            streams.push_back(track.stream);
        }
    }
    return streams;
}

void AudioSource::ApplyAllTrackGains()
{
    for (int i = 0; i < static_cast<int>(m_tracks.size()); ++i) {
        ApplyTrackGain(i);
    }
}

void AudioSource::ApplyTrackGain(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) {
        return;
    }
    auto &track = m_tracks[trackIndex];
    if (!track.stream || !track.isPlaying) {
        return;
    }
    if (track.isPaused) {
        SDL_SetAudioStreamGain(track.stream, 0.0f);
        return;
    }

    // Final gain = sourceVolume * trackVolume * spatialGain * (mute ? 0 : 1)
    float gain = m_mute ? 0.0f : (m_volume * track.volume * m_spatialGain);
    SDL_SetAudioStreamGain(track.stream, gain);
}

// ============================================================================
// Internal voice management
// ============================================================================

void AudioSource::StartVoice(int trackIndex)
{
    auto &engine = AudioEngine::Instance();
    if (!engine.IsInitialized()) {
        INFLOG_WARN("AudioSource::StartVoice: AudioEngine not initialized");
        return;
    }

    auto &track = m_tracks[trackIndex];
    track.stream = engine.CreateVoice(this, track.clip.get());
    if (!track.stream) {
        INFLOG_ERROR("AudioSource::StartVoice: failed to create voice for track ", trackIndex);
        return;
    }

    track.isPlaying = true;
    track.isPaused = false;
    ApplyTrackGain(trackIndex);
}

void AudioSource::StopVoice(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) {
        return;
    }
    auto &track = m_tracks[trackIndex];
    if (track.stream) {
        AudioEngine::Instance().DestroyVoice(track.stream);
        track.stream = nullptr;
    }
    track.isPlaying = false;
    track.isPaused = false;
}

void AudioSource::CheckLooping(int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= static_cast<int>(m_tracks.size())) {
        return;
    }
    auto &track = m_tracks[trackIndex];
    if (!track.clip || !track.clip->IsLoaded() || !track.stream) {
        return;
    }

    // Re-feed the clip data for looping
    const auto &data = track.clip->GetData();
    if (!SDL_PutAudioStreamData(track.stream, data.data(), static_cast<int>(data.size()))) {
        INFLOG_WARN("AudioSource: failed to re-feed loop data for track ", trackIndex, ": ", SDL_GetError());
        StopVoice(trackIndex);
    }
}

} // namespace infengine
