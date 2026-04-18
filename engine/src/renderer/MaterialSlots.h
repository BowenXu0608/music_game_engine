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
    Bandori = 0,
    Phigros,
    Cytus,
    Lanota,
    Arcaea,     // Phase 3 placeholder — empty slot list for now
    Count
};

// Mode-tagged slot list. Returned by reference-to-static storage so pointers
// remain stable across calls.
const std::vector<MaterialSlotInfo>& getMaterialSlotsForMode(MaterialModeKey mode);
