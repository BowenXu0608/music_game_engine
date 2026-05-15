#pragma once
#include "Material.h"
#include <string>
#include <array>
#include <vector>
#include <filesystem>

// Material assets — project-level resources that live on disk as JSON files
// under `project/assets/materials/<name>.mat`. A chart references assets by
// name; the renderer resolves the reference at draw time via
// `MaterialAssetLibrary::get()`.
//
// As of the PBR rework a MaterialAsset has a `cls`:
//   * Pbr           — the real material system (metallic-roughness). Lit by
//                      the renderer's preinstalled directional + ambient light.
//   * SpecialEffect — the legacy stylized fragment shaders (Glow/Scroll/Pulse/
//                      Gradient/Custom). Retained for back-compat; not
//                      authorable from the material picker.
//
// On-disk schema is versioned. v2 carries `"version":2` + `"class"`. Files
// with no `version` field are read as legacy v1 (a flat `kind`) and migrated
// in memory: `unlit` → plain PBR, `glow` → emissive PBR, everything else →
// SpecialEffect retaining its kind/params. v1 files are rewritten as v2 the
// next time the asset is upserted.

struct MaterialAsset {
    std::string            name;               // unique within a project
    MaterialClass          cls = MaterialClass::Pbr;

    // ── PBR class ───────────────────────────────────────────────────────────
    std::array<float, 4>   baseColor         = {1.f, 1.f, 1.f, 1.f};
    float                  metallic          = 0.f;
    float                  roughness         = 0.5f;
    std::array<float, 3>   emissiveColor     = {0.f, 0.f, 0.f};
    float                  emissiveIntensity = 0.f;
    bool                   useNormalMap      = false;
    std::string            baseColorTexPath;   // relative to project root
    std::string            normalTexPath;      // relative to project root

    // ── SpecialEffect class (legacy v1 fields) ──────────────────────────────
    MaterialKind           kind   = MaterialKind::Unlit;
    std::array<float, 4>   tint   = {1.f, 1.f, 1.f, 1.f};
    std::array<float, 4>   params = {0.f, 0.f, 0.f, 0.f};
    std::string            texturePath;        // relative to project root
    std::string            customShaderPath;   // Custom-kind only, project-relative

    // Compatibility constraints for slot-aware pickers. A material appears in
    // a SongEditor slot dropdown only when both fields match the slot's
    // (mode, slug), OR the field is empty ("any").
    std::string            targetMode;         // "bandori"/"arcaea"/"cytus"/"lanota"/"phigros"/""
    std::string            targetSlotSlug;     // e.g. "click_note", "playfield_ground", ""

    // Transient (not serialized): true when this asset was read from a legacy
    // v1 file and migrated in memory. The library uses it to rewrite the file
    // as v2 during seedDefaultMaterials so migration persists.
    bool                   wasLegacyV1 = false;
};

// Map the legacy `kind`/`tint`/`params` of a v1 asset onto the PBR/effect
// model. unlit → plain PBR (baseColor = tint), glow → emissive PBR, anything
// else → SpecialEffect (kind/tint/params retained verbatim). Idempotent-safe
// to call on an already-populated asset only when migrating.
void migrateAssetV1toV2(MaterialAsset& a);

// File-level helpers. `savePath` is the full path to the `.mat` file to write
// or read. Both helpers return true on success. `saveMaterialAsset` always
// writes the v2 schema.
bool saveMaterialAsset(const MaterialAsset& asset, const std::filesystem::path& savePath);
bool loadMaterialAsset(const std::filesystem::path& loadPath, MaterialAsset& out);
