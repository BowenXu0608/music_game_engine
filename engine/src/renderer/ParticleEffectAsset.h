#pragma once
#include "ParticleSystem.h"   // ParticleEmit
#include <string>
#include <array>
#include <filesystem>

// Particle-effect assets — project-level resources stored on disk as JSON under
// `project/assets/particles/<name>.pfx`. A chart/GameModeConfig references an
// effect by name; the engine resolves it at hit time via ParticleEffectLibrary.
//
// Modeled on MaterialAsset: a built-in `kind` selects an emission pattern (and,
// for Custom, a user fragment shader compiled through ShaderCompiler). The
// `targetMode`/`targetSlotSlug` fields drive slot-aware pickers exactly like
// materials (see MaterialAsset.h).

enum class ParticleEffectKind {
    Burst,        // radial scatter (the classic hit pop)
    Spark,        // directional cone + gravity
    Ring,         // uniform expanding ring
    Aura,         // slow sustained sparkle (used for held notes)
    Custom        // user .frag via ShaderCompiler
};

const char* particleKindName(ParticleEffectKind k);
ParticleEffectKind parseParticleKind(const std::string& s);

struct ParticleEffectAsset {
    std::string          name;                 // unique within a project

    ParticleEffectKind   kind     = ParticleEffectKind::Burst;

    // Emission parameters (flat — resolved into a ParticleEmit at runtime).
    int                  count    = 18;
    float                speedMin = 120.f;
    float                speedMax = 260.f;
    float                sizeStart = 9.f;
    float                sizeEnd   = 2.f;
    float                lifeMin  = 0.35f;
    float                lifeMax  = 0.6f;
    float                spread   = 6.2831853f; // radians (cone for Spark/Aura)
    std::array<float, 2> gravity  = {0.f, 0.f};
    float                drag     = 0.92f;
    float                rateHz   = 90.f;       // sustained rate (Aura)
    std::array<float, 4> color    = {0.3f, 1.f, 0.5f, 1.f};
    std::array<float, 4> colorEnd = {0.3f, 1.f, 0.5f, 0.f};

    std::string          customShaderPath;      // Custom-kind, project-relative

    // Slot-aware picker constraints (see MaterialAsset). Empty = "any".
    std::string          targetMode;            // "drop2d"/...
    std::string          targetSlotSlug;        // e.g. "click_hit"
};

// Build a ParticleEmit from the asset. `pipeKey` is supplied by the caller
// (0 for built-in kinds; a registered custom pipeline key for Custom).
ParticleEmit particleEmitFromAsset(const ParticleEffectAsset& a, uint16_t pipeKey);

// File helpers (full path to the .pfx). Both return true on success.
bool saveParticleEffectAsset(const ParticleEffectAsset& asset,
                             const std::filesystem::path& savePath);
bool loadParticleEffectAsset(const std::filesystem::path& loadPath,
                             ParticleEffectAsset& out);
