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

    // Returns current playback position in seconds (DSP clock).
    // Returns -1.0 if not playing.
    double positionSeconds() const;

    bool isPlaying() const { return m_playing; }

    // Play a short synthesized click sound (for editor note placement).
    void playClickSfx();

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
    struct Impl;
    Impl* m_impl = nullptr;
    bool  m_playing = false;
    float m_musicVolume     = 1.f;
    float m_sfxVolume       = 1.f;
    bool  m_hitSoundEnabled = true;
};
