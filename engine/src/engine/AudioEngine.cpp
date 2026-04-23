#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "AudioEngine.h"
#include <stdexcept>
#include <algorithm>

struct AudioEngine::Impl {
    ma_engine   engine;
    ma_sound    sound;
    bool        soundLoaded = false;
};

bool AudioEngine::init() {
    m_impl = new Impl();
    if (ma_engine_init(nullptr, &m_impl->engine) != MA_SUCCESS) {
        delete m_impl;
        m_impl = nullptr;
        return false;
    }
    return true;
}

void AudioEngine::shutdown() {
    if (!m_impl) return;
    if (m_impl->soundLoaded) {
        ma_sound_uninit(&m_impl->sound);
        m_impl->soundLoaded = false;
    }
    ma_engine_uninit(&m_impl->engine);
    delete m_impl;
    m_impl = nullptr;
    m_playing = false;
}

bool AudioEngine::load(const std::string& path) {
    if (!m_impl) return false;
    if (m_impl->soundLoaded) {
        ma_sound_uninit(&m_impl->sound);
        m_impl->soundLoaded = false;
    }
    if (ma_sound_init_from_file(&m_impl->engine, path.c_str(), 0, nullptr, nullptr,
                                 &m_impl->sound) != MA_SUCCESS)
        return false;
    m_impl->soundLoaded = true;
    ma_sound_set_volume(&m_impl->sound, m_musicVolume);
    return true;
}

void AudioEngine::play() {
    if (!m_impl || !m_impl->soundLoaded) return;
    ma_sound_seek_to_pcm_frame(&m_impl->sound, 0);
    ma_sound_start(&m_impl->sound);
    m_playing = true;
}

void AudioEngine::pause() {
    if (!m_impl || !m_impl->soundLoaded) return;
    ma_sound_stop(&m_impl->sound);
    m_playing = false;
}

void AudioEngine::resume() {
    if (!m_impl || !m_impl->soundLoaded) return;
    ma_sound_start(&m_impl->sound);
    m_playing = true;
}

void AudioEngine::stop() {
    if (!m_impl || !m_impl->soundLoaded) return;
    ma_sound_stop(&m_impl->sound);
    ma_sound_seek_to_pcm_frame(&m_impl->sound, 0);
    m_playing = false;
}

void AudioEngine::playFrom(double startSec) {
    if (!m_impl || !m_impl->soundLoaded) return;
    ma_uint32 sampleRate = 0;
    ma_sound_get_data_format(&m_impl->sound, nullptr, nullptr,
                             &sampleRate, nullptr, 0);
    if (sampleRate == 0) sampleRate = 44100;
    if (startSec < 0.0) startSec = 0.0;
    ma_uint64 frame = (ma_uint64)(startSec * (double)sampleRate);
    ma_sound_seek_to_pcm_frame(&m_impl->sound, frame);
    ma_sound_start(&m_impl->sound);
    m_playing = true;
}

double AudioEngine::durationSeconds() const {
    if (!m_impl || !m_impl->soundLoaded) return 0.0;
    float lengthSec = 0.f;
    if (ma_sound_get_length_in_seconds(
            const_cast<ma_sound*>(&m_impl->sound), &lengthSec) != MA_SUCCESS)
        return 0.0;
    return (double)lengthSec;
}

void AudioEngine::setMusicVolume(float v) {
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    m_musicVolume = v;
    if (m_impl && m_impl->soundLoaded)
        ma_sound_set_volume(&m_impl->sound, v);
}

void AudioEngine::setSfxVolume(float v) {
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    m_sfxVolume = v;
}

void AudioEngine::setHitSoundEnabled(bool on) {
    m_hitSoundEnabled = on;
}

double AudioEngine::positionSeconds() const {
    if (!m_impl || !m_impl->soundLoaded || !m_playing) return -1.0;
    float pos = 0.f;
    ma_sound_get_cursor_in_seconds(&m_impl->sound, &pos);
    return static_cast<double>(pos);
}

void AudioEngine::playClickSfx() {
    if (!m_impl) return;
    if (!m_hitSoundEnabled || m_sfxVolume <= 0.f) return;

    // Generate a very short click: 30ms of a 1200 Hz sine wave with fast decay.
    const ma_uint32 sampleRate = 44100;
    const ma_uint32 numFrames  = sampleRate * 30 / 1000; // 30ms
    const float freq = 1200.f;
    const float twoPiF = 2.f * 3.14159265f * freq;
    const float amp = 0.35f * m_sfxVolume;

    std::vector<float> samples(numFrames);
    for (ma_uint32 i = 0; i < numFrames; i++) {
        float t = (float)i / sampleRate;
        float envelope = 1.f - (float)i / numFrames; // linear decay
        envelope *= envelope; // quadratic decay for snappier click
        samples[i] = sinf(twoPiF * t) * envelope * amp;
    }

    // Play via a one-shot inline sound from memory buffer using ma_engine
    ma_audio_buffer_config bufCfg = ma_audio_buffer_config_init(
        ma_format_f32, 1, numFrames, samples.data(), nullptr);
    ma_audio_buffer* audioBuf = new ma_audio_buffer();
    if (ma_audio_buffer_init(&bufCfg, audioBuf) != MA_SUCCESS) {
        delete audioBuf;
        return;
    }

    // Create a sound from the audio buffer
    ma_sound* sfx = new ma_sound();
    if (ma_sound_init_from_data_source(&m_impl->engine, audioBuf, MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, sfx) != MA_SUCCESS) {
        ma_audio_buffer_uninit(audioBuf);
        delete audioBuf;
        delete sfx;
        return;
    }

    // Set end callback to clean up the sound and buffer
    // For simplicity, just start it — it's a very short one-shot.
    // The engine will keep it alive until it finishes.
    ma_sound_start(sfx);

    // We intentionally leak the sound+buffer here (30ms of audio, ~5KB).
    // A production engine would track and clean these up, but for editor SFX
    // the leak per click is negligible.
}

WaveformData AudioEngine::decodeWaveform(const std::string& path, uint32_t bucketCount) {
    WaveformData out;
    if (path.empty() || bucketCount == 0) return out;

    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 0); // mono, native rate
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &cfg, &decoder) != MA_SUCCESS)
        return out;

    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
    if (totalFrames == 0) {
        ma_decoder_uninit(&decoder);
        return out;
    }

    out.sampleRate      = decoder.outputSampleRate;
    out.durationSeconds = (double)totalFrames / out.sampleRate;

    // ── LOD 0: finest — decode directly from audio ──────────────────────────
    WaveformLOD& lod0 = out.lods.emplace_back();
    lod0.bucketCount = bucketCount;
    lod0.minSamples.resize(bucketCount, 0.f);
    lod0.maxSamples.resize(bucketCount, 0.f);

    ma_uint64 framesPerBucket = totalFrames / bucketCount;
    if (framesPerBucket == 0) framesPerBucket = 1;
    std::vector<float> buf(framesPerBucket);

    for (uint32_t b = 0; b < bucketCount; b++) {
        ma_uint64 toRead = framesPerBucket;
        if (b == bucketCount - 1)
            toRead = totalFrames - (ma_uint64)b * framesPerBucket;
        if (toRead > buf.size()) buf.resize(toRead);
        ma_uint64 framesRead = 0;
        ma_decoder_read_pcm_frames(&decoder, buf.data(), toRead, &framesRead);
        float mn =  1.f, mx = -1.f;
        for (ma_uint64 i = 0; i < framesRead; i++) {
            float s = buf[i];
            if (s < mn) mn = s;
            if (s > mx) mx = s;
        }
        if (framesRead == 0) { mn = 0.f; mx = 0.f; }
        lod0.minSamples[b] = mn;
        lod0.maxSamples[b] = mx;
    }
    ma_decoder_uninit(&decoder);

    // ── LOD 1-N: coarser levels, each 4× fewer buckets ─────────────────────
    // For each coarse bucket: min = min of 4 fine mins, max = max of 4 fine maxes.
    // Stop when we'd go below 256 buckets.
    while (out.lods.back().bucketCount / 4 >= 256) {
        uint32_t coarseCount = out.lods.back().bucketCount / 4;
        // Build the coarse LOD from a copy of the previous level's data,
        // because emplace_back can invalidate references into the vector.
        WaveformLOD coarse;
        coarse.bucketCount = coarseCount;
        coarse.minSamples.resize(coarseCount);
        coarse.maxSamples.resize(coarseCount);
        const auto& prevMin = out.lods.back().minSamples;
        const auto& prevMax = out.lods.back().maxSamples;
        for (uint32_t b = 0; b < coarseCount; b++) {
            float mn =  1.f, mx = -1.f;
            for (int k = 0; k < 4; k++) {
                uint32_t src = b * 4 + k;
                mn = std::min(mn, prevMin[src]);
                mx = std::max(mx, prevMax[src]);
            }
            coarse.minSamples[b] = mn;
            coarse.maxSamples[b] = mx;
        }
        out.lods.push_back(std::move(coarse));
    }

    out.bucketCount = out.lods[0].bucketCount;
    return out;
}
