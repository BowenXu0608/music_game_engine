#pragma once
#include <string>
#include <vector>
#include <cstdint>

// One resolution level of the waveform envelope.
struct WaveformLOD {
    std::vector<float> minSamples;
    std::vector<float> maxSamples;
    uint32_t bucketCount = 0;
};

// Multi-LOD waveform envelope for visualization.
// lods[0] = finest (most buckets), lods.back() = coarsest.
// At render time, pick the coarsest LOD that still has >= 1 bucket per pixel.
struct WaveformData {
    std::vector<WaveformLOD> lods;
    double   durationSeconds = 0.0;
    uint32_t sampleRate      = 0;
    uint32_t bucketCount     = 0;   // = lods[0].bucketCount when loaded
};

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

    // Seek to `startSec` and start playback. Used by the Music Selection
    // page to play a short preview clip of a highlighted song.
    void playFrom(double startSec);

    // Total duration of the currently-loaded sound (seconds). 0 if none.
    double durationSeconds() const;

    // Returns current playback position in seconds (DSP clock).
    // Returns -1.0 if not playing.
    double positionSeconds() const;

    bool isPlaying() const { return m_playing; }

    // Play a short synthesized click sound (for editor note placement).
    void playClickSfx();

    // Fire-and-forget playback of an audio file as a one-shot SFX. Mixed on a
    // dedicated SFX group so it layers over the music/preview stream without
    // interrupting it (used for the Music Selection wheel move/click sounds).
    // Empty path or zero SFX volume is a no-op. Path is UTF-8.
    void playSfxFile(const std::string& path);

    // Preload an audio file into an in-memory cache under `key`. Decoded once;
    // subsequent playCachedSfx(key) plays cheap instances with no disk read or
    // per-call decode — required for dense SFX (note clicks, hold ticks) that
    // would otherwise stutter the audio thread (see the no-tick-SFX note that
    // this replaces in Engine::update). Idempotent: re-preloading an existing
    // key is a no-op. Path is UTF-8.
    void preloadSfx(const std::string& key, const std::string& path);

    // Play a one-shot from the preloaded cache. `minIntervalMs` rate-limits a
    // key (skipped if it last played more recently than that) so dense hold
    // ticks self-throttle. No-op if muted, volume 0, or `key` not preloaded.
    void playCachedSfx(const std::string& key, float minIntervalMs = 0.f);

    // Looping SFX, for the sustained tone while a hold note is held.
    // startLoopingSfx returns a handle (0 on failure) used to stop it later;
    // the caller keys these by hold note id. Path is UTF-8.
    uint32_t startLoopingSfx(const std::string& path);
    void     stopLoopingSfx(uint32_t handle);
    void     stopAllLoopingSfx();

    // Player-settings hooks.
    void setMusicVolume(float v);       // 0..1
    void setSfxVolume(float v);         // 0..1
    void setHitSoundEnabled(bool on);
    float musicVolume() const { return m_musicVolume; }
    float sfxVolume()   const { return m_sfxVolume; }
    bool  hitSoundEnabled() const { return m_hitSoundEnabled; }

    // Decode audio file into a multi-LOD waveform envelope.
    // bucketCount sets the finest LOD; 3 coarser levels are derived automatically.
    static WaveformData decodeWaveform(const std::string& path,
                                       uint32_t bucketCount = 65536);

private:
    void reapFinishedSfx();

    struct Impl;
    Impl* m_impl = nullptr;
    bool  m_playing = false;
    float m_musicVolume     = 1.f;
    float m_sfxVolume       = 1.f;
    bool  m_hitSoundEnabled = true;
};
