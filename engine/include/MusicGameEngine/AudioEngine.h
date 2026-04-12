#pragma once
#include <string>

// Thin wrapper around miniaudio for music playback + DSP clock query.
class AudioEngine {
public:
    bool init();
    void shutdown();

    bool load(const std::string& path);
    void play();
    void pause();
    void resume();
    void stop();

    // Returns current playback position in seconds (DSP clock).
    // Returns -1.0 if not playing.
    double positionSeconds() const;

    bool isPlaying() const { return m_playing; }

    // Short click SFX played on per-hold sample-tick hits.
    void playClickSfx();

private:
    struct Impl;
    Impl* m_impl = nullptr;
    bool  m_playing = false;
};
