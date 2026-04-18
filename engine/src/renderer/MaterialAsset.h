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
// Built-in MaterialKinds (Unlit/Glow/Scroll/Pulse/Gradient) are primitives
// that a MaterialAsset selects as its `kind`. `Custom` is the sixth option
// and carries a path to a user-authored fragment shader. See
// `MaterialAssetLibrary` for compile/cache behaviour of Custom shaders.

struct MaterialAsset {
    std::string            name;               // unique within a project
    MaterialKind           kind   = MaterialKind::Unlit;
    std::array<float, 4>   tint   = {1.f, 1.f, 1.f, 1.f};
    std::array<float, 4>   params = {0.f, 0.f, 0.f, 0.f};
    std::string            texturePath;        // relative to project root
    // Custom-kind only. Path relative to project root. Ignored otherwise.
    std::string            customShaderPath;
    // Compatibility constraints for slot-aware pickers. A material appears in
    // a SongEditor slot dropdown only when both fields match the slot's
    // (mode, slug), OR the field is empty ("any"). Seeded defaults and
    // migrated overrides always populate both; user-created assets default
    // to empty (universal) unless the user picks a target.
    std::string            targetMode;         // "bandori"/"arcaea"/"cytus"/"lanota"/"phigros"/""
    std::string            targetSlotSlug;     // e.g. "click_note", "playfield_ground", ""
};

// File-level helpers. `savePath` is the full path to the `.mat` file to write
// or read. Both helpers return true on success.
bool saveMaterialAsset(const MaterialAsset& asset, const std::filesystem::path& savePath);
bool loadMaterialAsset(const std::filesystem::path& loadPath, MaterialAsset& out);
