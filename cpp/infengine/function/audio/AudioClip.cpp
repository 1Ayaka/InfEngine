#include "AudioClip.h"
#include <core/log/InfLog.h>
#include <function/resources/InfResource/InfResourceMeta.h>

#include <SDL3/SDL.h>
#include <cstring>
#include <filesystem>

namespace infengine
{

AudioClip::~AudioClip()
{
    Unload();
}

AudioClip::AudioClip(AudioClip &&other) noexcept
    : m_loaded(other.m_loaded), m_filePath(std::move(other.m_filePath)), m_name(std::move(other.m_name)),
      m_spec(other.m_spec), m_data(std::move(other.m_data)), m_dataLength(other.m_dataLength)
{
    other.m_loaded = false;
    other.m_spec = {};
    other.m_dataLength = 0;
}

AudioClip &AudioClip::operator=(AudioClip &&other) noexcept
{
    if (this != &other) {
        Unload();
        m_loaded = other.m_loaded;
        m_filePath = std::move(other.m_filePath);
        m_name = std::move(other.m_name);
        m_spec = other.m_spec;
        m_data = std::move(other.m_data);
        m_dataLength = other.m_dataLength;

        other.m_loaded = false;
        other.m_spec = {};
        other.m_dataLength = 0;
    }
    return *this;
}

bool AudioClip::LoadFromFile(const std::string &filePath)
{
    if (m_loaded) {
        Unload();
    }

    // Use SDL3 to load WAV files
    Uint8 *audioBuffer = nullptr;
    Uint32 audioLength = 0;
    SDL_AudioSpec wavSpec = {};

    if (!SDL_LoadWAV(filePath.c_str(), &wavSpec, &audioBuffer, &audioLength)) {
        INFLOG_ERROR("Failed to load WAV file '", filePath, "': ", SDL_GetError());
        return false;
    }

    // Copy data to our own buffer (SDL_LoadWAV uses SDL_malloc)
    m_data.assign(audioBuffer, audioBuffer + audioLength);
    SDL_free(audioBuffer);

    m_spec = wavSpec;
    m_dataLength = audioLength;
    m_filePath = filePath;

    // Extract name from path
    std::filesystem::path fsPath(filePath);
    m_name = fsPath.stem().string();

    m_loaded = true;

    // ── Apply import settings from .meta ──────────────────────────────
    ApplyImportSettings();

    INFLOG_DEBUG("AudioClip loaded: '", m_name, "' (", m_spec.freq, " Hz, ", m_spec.channels, " ch, ", m_dataLength,
                 " bytes)");

    return true;
}

void AudioClip::ApplyImportSettings()
{
    std::string metaPath = InfResourceMeta::GetMetaFilePath(m_filePath);
    InfResourceMeta meta;
    if (!meta.LoadFromFile(metaPath))
        return;

    // force_mono: convert stereo (or multi-channel) to mono
    if (meta.HasKey("force_mono")) {
        bool forceMono = false;
        try {
            forceMono = meta.GetDataAs<bool>("force_mono");
        } catch (...) {
            // Might be stored as int/string in some meta files
        }

        if (forceMono && m_spec.channels > 1) {
            ConvertToMono();
        }
    }
}

void AudioClip::ConvertToMono()
{
    if (m_spec.channels <= 1)
        return;

    int bytesPerSample = SDL_AUDIO_BYTESIZE(m_spec.format);
    if (bytesPerSample == 0)
        return;

    int channels = m_spec.channels;
    uint32_t frameCount = m_dataLength / (bytesPerSample * channels);
    std::vector<uint8_t> monoData(frameCount * bytesPerSample);

    bool isFloat = SDL_AUDIO_ISFLOAT(m_spec.format);
    bool isSigned = SDL_AUDIO_ISSIGNED(m_spec.format);

    for (uint32_t f = 0; f < frameCount; ++f) {
        if (isFloat && bytesPerSample == 4) {
            // Float32 mixing
            float sum = 0.0f;
            for (int ch = 0; ch < channels; ++ch) {
                float sample;
                std::memcpy(&sample, &m_data[(f * channels + ch) * sizeof(float)], sizeof(float));
                sum += sample;
            }
            float mono = sum / static_cast<float>(channels);
            std::memcpy(&monoData[f * sizeof(float)], &mono, sizeof(float));
        } else if (bytesPerSample == 2 && isSigned) {
            // Sint16 mixing
            int32_t sum = 0;
            for (int ch = 0; ch < channels; ++ch) {
                int16_t sample;
                std::memcpy(&sample, &m_data[(f * channels + ch) * sizeof(int16_t)], sizeof(int16_t));
                sum += sample;
            }
            int16_t mono = static_cast<int16_t>(sum / channels);
            std::memcpy(&monoData[f * sizeof(int16_t)], &mono, sizeof(int16_t));
        } else if (bytesPerSample == 1) {
            // Uint8 mixing
            int32_t sum = 0;
            for (int ch = 0; ch < channels; ++ch) {
                sum += m_data[f * channels + ch];
            }
            monoData[f] = static_cast<uint8_t>(sum / channels);
        } else {
            // Unsupported format — skip conversion
            INFLOG_WARN("AudioClip: unsupported format for mono conversion, skipping");
            return;
        }
    }

    m_data = std::move(monoData);
    m_dataLength = static_cast<uint32_t>(m_data.size());
    m_spec.channels = 1;

    INFLOG_DEBUG("AudioClip: converted to mono (", frameCount, " frames, ", m_dataLength, " bytes)");
}

void AudioClip::Unload()
{
    m_data.clear();
    m_data.shrink_to_fit();
    m_spec = {};
    m_dataLength = 0;
    m_loaded = false;
}

float AudioClip::GetDuration() const
{
    if (!m_loaded || m_spec.freq == 0 || m_spec.channels == 0) {
        return 0.0f;
    }

    int bytesPerSample = SDL_AUDIO_BYTESIZE(m_spec.format);
    if (bytesPerSample == 0) {
        return 0.0f;
    }

    uint32_t totalFrames = m_dataLength / (bytesPerSample * m_spec.channels);
    return static_cast<float>(totalFrames) / static_cast<float>(m_spec.freq);
}

uint32_t AudioClip::GetSampleCount() const
{
    if (!m_loaded || m_spec.channels == 0) {
        return 0;
    }

    int bytesPerSample = SDL_AUDIO_BYTESIZE(m_spec.format);
    if (bytesPerSample == 0) {
        return 0;
    }

    return m_dataLength / (bytesPerSample * m_spec.channels);
}

} // namespace infengine
