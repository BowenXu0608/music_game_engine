#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "AudioEngine.h"
#include <stdexcept>

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

double AudioEngine::positionSeconds() const {
    if (!m_impl || !m_impl->soundLoaded || !m_playing) return -1.0;
    float pos = 0.f;
    ma_sound_get_cursor_in_seconds(&m_impl->sound, &pos);
    return static_cast<double>(pos);
}
