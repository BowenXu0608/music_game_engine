#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "AudioEngine.h"
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#include <string>
#endif

namespace {
// Monotonic milliseconds for SFX throttle bookkeeping.
double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
}

struct AudioEngine::Impl {
    ma_engine   engine;
    ma_sound    sound;
    bool        soundLoaded = false;

    // Dedicated group for fire-and-forget file SFX (wheel sounds, etc.) so they
    // mix over the music stream. Finished one-shots are reaped lazily.
    ma_sound_group        sfxGroup;
    bool                  sfxGroupInit = false;

    // An active one-shot. `buf` is non-null only for cache-sourced playback
    // (it must be uninited alongside the sound); file-sourced one-shots own
    // their data internally and leave `buf` null.
    struct ActiveSfx { ma_sound* sound = nullptr; ma_audio_buffer* buf = nullptr; };
    std::vector<ActiveSfx> sfxSounds;

    // Decoded-once PCM kept resident so dense SFX play without disk/decode cost.
    // Multiple ma_audio_buffers reference the same read-only `frames` with
    // independent cursors, so simultaneous plays of one key are safe.
    struct CachedSfx {
        std::vector<float> frames;
        ma_uint32 channels   = 0;
        ma_uint32 sampleRate = 0;
        ma_uint64 frameCount = 0;
    };
    std::unordered_map<std::string, CachedSfx> sfxCache;
    std::unordered_map<std::string, double>    lastPlayMs;   // throttle by key

    // Looping SFX (sustained hold tone), keyed by an opaque handle.
    std::unordered_map<uint32_t, ma_sound*> loopSounds;
    uint32_t nextLoopHandle = 1;
};

bool AudioEngine::init() {
    m_impl = new Impl();
    if (ma_engine_init(nullptr, &m_impl->engine) != MA_SUCCESS) {
        delete m_impl;
        m_impl = nullptr;
        return false;
    }
    if (ma_sound_group_init(&m_impl->engine, 0, nullptr, &m_impl->sfxGroup) == MA_SUCCESS) {
        m_impl->sfxGroupInit = true;
        ma_sound_group_set_volume(&m_impl->sfxGroup, m_sfxVolume);
    }
    return true;
}

void AudioEngine::shutdown() {
    if (!m_impl) return;
    stopAllLoopingSfx();
    for (auto& a : m_impl->sfxSounds) {
        ma_sound_uninit(a.sound);
        delete a.sound;
        if (a.buf) { ma_audio_buffer_uninit(a.buf); delete a.buf; }
    }
    m_impl->sfxSounds.clear();
    m_impl->sfxCache.clear();
    if (m_impl->sfxGroupInit) {
        ma_sound_group_uninit(&m_impl->sfxGroup);
        m_impl->sfxGroupInit = false;
    }
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
#ifdef _WIN32
    // miniaudio's narrow ma_sound_init_from_file uses CreateFileA which
    // interprets the path as CP_ACP, so non-ASCII filenames stored as UTF-8
    // (e.g. 中村由利子+-+Whispering+Eyes.mp3) fail to open. Convert to wide
    // and use the _w variant which goes through CreateFileW directly.
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
    bool used_w = false;
    if (wlen > 0) {
        std::wstring wPath(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wPath[0], wlen);
        if (ma_sound_init_from_file_w(&m_impl->engine, wPath.c_str(), 0, nullptr, nullptr,
                                      &m_impl->sound) == MA_SUCCESS) {
            used_w = true;
        }
    }
    if (!used_w) {
        if (ma_sound_init_from_file(&m_impl->engine, path.c_str(), 0, nullptr, nullptr,
                                     &m_impl->sound) != MA_SUCCESS)
            return false;
    }
#else
    if (ma_sound_init_from_file(&m_impl->engine, path.c_str(), 0, nullptr, nullptr,
                                 &m_impl->sound) != MA_SUCCESS)
        return false;
#endif
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
    if (m_impl && m_impl->sfxGroupInit)
        ma_sound_group_set_volume(&m_impl->sfxGroup, v);
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

void AudioEngine::reapFinishedSfx() {
    if (!m_impl) return;
    for (auto it = m_impl->sfxSounds.begin(); it != m_impl->sfxSounds.end(); ) {
        if (ma_sound_at_end(it->sound)) {
            ma_sound_uninit(it->sound);
            delete it->sound;
            if (it->buf) { ma_audio_buffer_uninit(it->buf); delete it->buf; }
            it = m_impl->sfxSounds.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioEngine::playSfxFile(const std::string& path) {
    if (!m_impl || !m_impl->sfxGroupInit) return;
    if (path.empty() || m_sfxVolume <= 0.f) return;

    // Reap any finished one-shots so the tracking list stays bounded.
    reapFinishedSfx();

    ma_sound* s = new ma_sound();
    bool ok = false;
#ifdef _WIN32
    // Same UTF-8 path handling as load(): the narrow init goes through
    // CreateFileA / CP_ACP and fails on non-ASCII paths stored as UTF-8.
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
    if (wlen > 0) {
        std::wstring wPath(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wPath[0], wlen);
        if (ma_sound_init_from_file_w(&m_impl->engine, wPath.c_str(),
                                      MA_SOUND_FLAG_NO_SPATIALIZATION,
                                      &m_impl->sfxGroup, nullptr, s) == MA_SUCCESS)
            ok = true;
    }
#endif
    if (!ok) {
        if (ma_sound_init_from_file(&m_impl->engine, path.c_str(),
                                    MA_SOUND_FLAG_NO_SPATIALIZATION,
                                    &m_impl->sfxGroup, nullptr, s) != MA_SUCCESS) {
            delete s;
            return;
        }
    }
    ma_sound_start(s);
    m_impl->sfxSounds.push_back({s, nullptr});
}

void AudioEngine::preloadSfx(const std::string& key, const std::string& path) {
    if (!m_impl || key.empty() || path.empty()) return;
    if (m_impl->sfxCache.count(key)) return;   // idempotent

    // Decode the whole file into native-format interleaved f32 frames.
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0); // native ch/rate
    ma_decoder decoder;
    bool decoder_ok = false;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
    if (wlen > 0) {
        std::wstring wPath(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wPath[0], wlen);
        if (ma_decoder_init_file_w(wPath.c_str(), &cfg, &decoder) == MA_SUCCESS)
            decoder_ok = true;
    }
#endif
    if (!decoder_ok) {
        if (ma_decoder_init_file(path.c_str(), &cfg, &decoder) != MA_SUCCESS)
            return;
    }

    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames);
    ma_uint32 channels = decoder.outputChannels;
    if (totalFrames == 0 || channels == 0) {
        ma_decoder_uninit(&decoder);
        return;
    }

    Impl::CachedSfx c;
    c.channels   = channels;
    c.sampleRate = decoder.outputSampleRate;
    c.frameCount = totalFrames;
    c.frames.resize((size_t)totalFrames * channels);
    ma_uint64 framesRead = 0;
    ma_decoder_read_pcm_frames(&decoder, c.frames.data(), totalFrames, &framesRead);
    c.frameCount = framesRead;
    ma_decoder_uninit(&decoder);
    if (framesRead == 0) return;

    m_impl->sfxCache.emplace(key, std::move(c));
}

void AudioEngine::playCachedSfx(const std::string& key, float minIntervalMs) {
    if (!m_impl || !m_impl->sfxGroupInit || m_sfxVolume <= 0.f) return;
    auto cit = m_impl->sfxCache.find(key);
    if (cit == m_impl->sfxCache.end()) return;

    if (minIntervalMs > 0.f) {
        double t = nowMs();
        auto lit = m_impl->lastPlayMs.find(key);
        if (lit != m_impl->lastPlayMs.end() && (t - lit->second) < minIntervalMs)
            return;
        m_impl->lastPlayMs[key] = t;
    }

    reapFinishedSfx();

    const Impl::CachedSfx& c = cit->second;
    // Each play gets its own audio-buffer cursor over the shared read-only PCM.
    ma_audio_buffer_config bufCfg = ma_audio_buffer_config_init(
        ma_format_f32, c.channels, c.frameCount, c.frames.data(), nullptr);
    bufCfg.sampleRate = c.sampleRate;
    ma_audio_buffer* buf = new ma_audio_buffer();
    if (ma_audio_buffer_init(&bufCfg, buf) != MA_SUCCESS) {
        delete buf;
        return;
    }
    ma_sound* s = new ma_sound();
    if (ma_sound_init_from_data_source(&m_impl->engine, buf,
            MA_SOUND_FLAG_NO_PITCH | MA_SOUND_FLAG_NO_SPATIALIZATION,
            &m_impl->sfxGroup, s) != MA_SUCCESS) {
        ma_audio_buffer_uninit(buf);
        delete buf;
        delete s;
        return;
    }
    ma_sound_start(s);
    m_impl->sfxSounds.push_back({s, buf});
}

uint32_t AudioEngine::startLoopingSfx(const std::string& path) {
    if (!m_impl || !m_impl->sfxGroupInit) return 0;
    if (path.empty() || m_sfxVolume <= 0.f) return 0;

    ma_sound* s = new ma_sound();
    bool ok = false;
#ifdef _WIN32
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
    if (wlen > 0) {
        std::wstring wPath(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wPath[0], wlen);
        if (ma_sound_init_from_file_w(&m_impl->engine, wPath.c_str(),
                                      MA_SOUND_FLAG_NO_SPATIALIZATION,
                                      &m_impl->sfxGroup, nullptr, s) == MA_SUCCESS)
            ok = true;
    }
#endif
    if (!ok) {
        if (ma_sound_init_from_file(&m_impl->engine, path.c_str(),
                                    MA_SOUND_FLAG_NO_SPATIALIZATION,
                                    &m_impl->sfxGroup, nullptr, s) != MA_SUCCESS) {
            delete s;
            return 0;
        }
    }
    ma_sound_set_looping(s, MA_TRUE);
    ma_sound_start(s);
    uint32_t handle = m_impl->nextLoopHandle++;
    m_impl->loopSounds.emplace(handle, s);
    return handle;
}

void AudioEngine::stopLoopingSfx(uint32_t handle) {
    if (!m_impl || handle == 0) return;
    auto it = m_impl->loopSounds.find(handle);
    if (it == m_impl->loopSounds.end()) return;
    ma_sound_stop(it->second);
    ma_sound_uninit(it->second);
    delete it->second;
    m_impl->loopSounds.erase(it);
}

void AudioEngine::stopAllLoopingSfx() {
    if (!m_impl) return;
    for (auto& [handle, s] : m_impl->loopSounds) {
        ma_sound_stop(s);
        ma_sound_uninit(s);
        delete s;
    }
    m_impl->loopSounds.clear();
}

WaveformData AudioEngine::decodeWaveform(const std::string& path, uint32_t bucketCount) {
    WaveformData out;
    if (path.empty() || bucketCount == 0) return out;

    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 0); // mono, native rate
    ma_decoder decoder;
    bool decoder_ok = false;
#ifdef _WIN32
    // Same UTF-8 issue as AudioEngine::load — ma_decoder_init_file goes through
    // CreateFileA / CP_ACP, which fails on non-ASCII paths stored as UTF-8.
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), nullptr, 0);
    if (wlen > 0) {
        std::wstring wPath(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, path.data(), (int)path.size(), &wPath[0], wlen);
        if (ma_decoder_init_file_w(wPath.c_str(), &cfg, &decoder) == MA_SUCCESS)
            decoder_ok = true;
    }
#endif
    if (!decoder_ok) {
        if (ma_decoder_init_file(path.c_str(), &cfg, &decoder) != MA_SUCCESS)
            return out;
    }

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
