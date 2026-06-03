#pragma once
#include "ParticleEffectAsset.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

// Per-project registry of ParticleEffectAssets. Owns the on-disk directory
// `<project>/assets/particles/` and the in-memory name -> asset map. Mirrors
// MaterialAssetLibrary; see that header for the lifecycle rationale.
//
// Not thread-safe: main editor/render thread only.

class ParticleEffectLibrary {
public:
    // Clear and load every *.pfx under `<projectDir>/assets/particles/`.
    void loadFromProject(const std::filesystem::path& projectDir);
    void clear();

    const std::filesystem::path& projectDir() const { return m_projectDir; }

    const ParticleEffectAsset* get(const std::string& name) const;

    // Insert/overwrite in memory AND on disk (`<name>.pfx`). False on write fail.
    bool upsert(const ParticleEffectAsset& asset);
    void remove(const std::string& name);

    std::vector<std::string> allNames() const;

    // Slot-aware picker filter: assets whose targetMode/targetSlotSlug match the
    // given (mode, slug) or are empty.
    std::vector<std::string> namesCompatibleWith(const std::string& mode,
                                                 const std::string& slug) const;

    const std::unordered_map<std::string, ParticleEffectAsset>& all() const { return m_assets; }

    // Ensure each Bandori event slot has a `default_bandori_<slug>.pfx` on disk.
    // Pre-existing files are left untouched so user edits persist. Idempotent.
    void seedDefaultEffects(const std::string& mode);

    // Ensure the shared UI button-tap effect (`ui_tap.pfx`) exists. Mode-
    // independent; left untouched if already present (preserves edits). Idempotent.
    void seedUiTapEffect();

private:
    std::filesystem::path particlesDir() const;
    std::filesystem::path m_projectDir;
    std::unordered_map<std::string, ParticleEffectAsset> m_assets;
};
