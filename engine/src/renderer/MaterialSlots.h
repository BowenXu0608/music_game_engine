#pragma once
#include "Material.h"
#include <cstdint>
#include <vector>
#include <string>

// ── Material slots per game mode ────────────────────────────────────────────
// A slot is a named visual role within one game mode (e.g. "tap_note",
// "hold_body"). The renderer looks up its own default Material for each slot;
// users can override the defaults per chart via the SongEditor materials
// panel. Slots are identified by a uint16 id that is serialized into the
// chart JSON's "materials" array.

struct MaterialSlotInfo {
    uint16_t     id;            // stable across chart saves
    const char*  displayName;   // shown in the editor
    const char*  group;         // group header label ("" = top-level, no group)
    MaterialKind defaultKind;   // fallback when override is absent
    float        defaultTint[4];
    float        defaultParams[4];
};

// Each renderer provides its own slot list via a static accessor that returns
// a span/vector. GameModeType → slot list lookup lives in
// `getMaterialSlotsForMode()` below so SongEditor can query generically.

enum class MaterialModeKey : int {
    Drop2D = 0,
    Phigros,
    ScanLine,
    Circle,
    Drop3D,     // Phase 3 placeholder — empty slot list for now
    Count
};

// Mode-tagged slot list. Returned by reference-to-static storage so pointers
// remain stable across calls.
const std::vector<MaterialSlotInfo>& getMaterialSlotsForMode(MaterialModeKey mode);

// Filename-safe slug for a slot. Lowercase alphanumeric + underscores; group
// name is prefixed when present so distinct slots that happen to share a
// display name (e.g. "Head" under both Hold Note and Slide Note in ScanLine)
// don't collide. Used to build `default_<mode>_<slug>.mat` and
// `<chartStem>__<slug>.mat`.
std::string materialSlotSlug(const MaterialSlotInfo& slot);

// Short identifier for a mode, used in default file names.
const char* materialModeName(MaterialModeKey mode);

// Maps legacy mode tokens from older project files to the current generic
// tokens (drop2d/drop3d/scanline/circle). Pass-through for already-current or
// unrecognized tokens. Lets pre-rename .mat/.pfx files keep resolving.
std::string normalizeModeToken(const std::string& token);

// Infer a chart's MaterialModeKey from its filename stem. Relies on the
// engine's `<song>_<modeKey>_<difficulty>` convention (drop2d/drop3d/
// circle/scan/phigros). Defaults to Drop2D for unrecognized stems.
MaterialModeKey detectChartMode(const std::string& chartStem);
