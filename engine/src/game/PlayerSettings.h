#pragma once
#include <string>

// Player-facing runtime settings for the shipped Android music game.
// Persisted as JSON next to the project's assets (e.g. app-internal storage).
struct PlayerSettings {
    float       musicVolume     = 0.8f;    // 0..1
    float       hitSoundVolume  = 0.8f;    // 0..1
    bool        hitSoundEnabled = true;
    float       audioOffsetMs   = 0.0f;    // -200..+200
    float       noteSpeed       = 5.0f;    // 1..10, 5 = "default"
    float       backgroundDim   = 0.3f;    // 0..1
    bool        fpsCounter      = false;
    std::string language        = "en";
};

bool loadPlayerSettings(const std::string& path, PlayerSettings& out);
bool savePlayerSettings(const std::string& path, const PlayerSettings& s);
