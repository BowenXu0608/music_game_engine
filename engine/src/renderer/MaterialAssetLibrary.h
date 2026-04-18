#pragma once
#include "MaterialAsset.h"
#include "MaterialSlots.h"
#include "game/chart/ChartTypes.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

// Per-project registry of MaterialAssets. Owns the on-disk directory
// `<project>/assets/materials/` and the in-memory map from name → asset.
// Typical lifecycle:
//   * Engine::openProject(path) calls `loadFromProject(path)`.
//   * ChartLoader calls `createFromLegacyInline(...)` when it encounters a
//     pre-asset-library chart so the inline override becomes a proper asset.
//   * Renderers call `get(name)` at draw time to resolve slot assignments.
//   * Editor UIs call `upsert`/`remove`/`all` for CRUD.
//
// Not thread-safe: expected to be touched only from the main editor/render
// thread.

class MaterialAssetLibrary {
public:
    // Clears the library and loads every *.mat file under
    // `<projectDir>/assets/materials/`. Missing directory is fine — the
    // library stays empty.
    void loadFromProject(const std::filesystem::path& projectDir);

    // Empties the in-memory map. Does not touch disk.
    void clear();

    // Returns the project root last passed to loadFromProject(). Used by
    // upsert()/remove()/createFromLegacyInline() to compute on-disk paths.
    const std::filesystem::path& projectDir() const { return m_projectDir; }

    // Look up by name. Returns nullptr if not found.
    const MaterialAsset* get(const std::string& name) const;

    // Insert or overwrite in-memory AND write to disk. The filename is
    // `<name>.mat`. Returns false on disk-write failure.
    bool upsert(const MaterialAsset& asset);

    // Remove from memory and unlink the on-disk file. Missing entries are
    // silently ignored.
    void remove(const std::string& name);

    // Produce a stable sorted list of names (for UI dropdowns).
    std::vector<std::string> allNames() const;

    // Filtered variant for slot-aware pickers. Returns only assets whose
    // `targetMode`/`targetSlotSlug` either match the given (mode, slug) or
    // are empty (the "universal" case — user-created unconstrained assets).
    std::vector<std::string> namesCompatibleWith(MaterialModeKey mode,
                                                  const std::string& slug) const;

    // All assets, in insertion order (for enumeration/debug).
    const std::unordered_map<std::string, MaterialAsset>& all() const { return m_assets; }

    // Ensure every slot in `mode`'s slot table has a backing
    // `default_<mode>_<slug>.mat` on disk. Pre-existing default files are
    // left untouched so user edits in the Materials editor persist across
    // project re-opens. Safe to call multiple times.
    void seedDefaultMaterials(MaterialModeKey mode);

    // Walk `chart.materials` and, for each entry that still uses the legacy
    // inline form (empty `assetName`), either point it at the shared
    // `default_<mode>_<slug>` when values match the slot default, or create
    // a per-chart override named `<chartStem>__<slug>.mat`.
    // `chartStem` must be the chart filename without `.json`. Safe to call
    // multiple times — idempotent.
    void migrateChartToAssets(ChartData& chart,
                              const std::string& chartStem,
                              MaterialModeKey mode);

    // Delete orphaned `.mat` files produced by the Phase-A cryptic naming
    // scheme (`<chartStem>_<slotIdNumber>.mat`). Prunes only files whose
    // prefix matches one of the supplied chart stems so unrelated user
    // materials (e.g. `effect_v1`) don't get caught up.
    void pruneOldCrypticFiles(const std::vector<std::string>& chartStems);

private:
    // Absolute path to the project root. `<root>/assets/materials/` is where
    // .mat files are loaded from and saved to.
    std::filesystem::path m_projectDir;
    std::unordered_map<std::string, MaterialAsset> m_assets;

    std::filesystem::path materialsDir() const;
};

// Turn a ChartData::MaterialData entry into a runtime Material. If the entry
// references a named asset and `lib` has it, the asset wins; otherwise the
// inline legacy fields (kind/tint/params) are used. `texture`/`sampler` are
// NOT populated here — renderers resolve those against their Renderer's
// white fallback because textures are VkImageView-typed and this header is
// Vulkan-agnostic.
Material resolveMaterial(const ChartData::MaterialData& md,
                         const MaterialAssetLibrary* lib);
