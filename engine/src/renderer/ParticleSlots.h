#pragma once
#include <string>
#include <vector>

// Reserved, mode-independent effect for the UI button-tap feedback. One shared
// effect for every tappable widget on the player screens; seeded by
// ParticleEffectLibrary::seedUiTapEffect and editable in the FX tab.
inline constexpr char kUiTapEffectName[] = "ui_tap";

// Particle-effect event slots. Unlike material slots (which key off note
// geometry), particle slots key off gameplay EVENTS — the moments at which an
// effect should fire. Drop2D 2D-drop is the proving-ground mode; other modes
// reuse this vocabulary as they come online (Phase 5).

struct ParticleSlotInfo {
    std::string slug;     // stable id used in JSON + asset targetSlotSlug
    std::string label;    // human label for editor pickers
};

// Drop2D event slots, in display order.
//   click_hit  — a Click (tap) note judged non-miss
//   flick_hit  — a Flick note judged non-miss
//   hold_head  — the moment a hold is grabbed
//   hold_tick  — each hold body sample-tick that lands on the right lane
//   hold_aura  — sustained sparkle emitted every frame while a hold is held
//   hold_end   — a hold released at its tail
const std::vector<ParticleSlotInfo>& drop2dParticleSlots();

// ScanLine event slots. Adds `slide_tick` — every slide sample point
// that the scan line passes / the finger tracks (ScanLine-only note type).
const std::vector<ParticleSlotInfo>& scanlineParticleSlots();

// Drop3D (3D drop) event slots. `click_hit` = ground lane taps; `sky_hit` =
// arc / arc-tap (sky) events. NOTE: drop3d effects emit in WORLD space, so the
// seeded defaults use world-scale speed/size (see defaultForSlot).
const std::vector<ParticleSlotInfo>& drop3dParticleSlots();

// Circle (Circle/radial) event slots. Emits in screen space like Drop2D.
const std::vector<ParticleSlotInfo>& circleParticleSlots();

// Slot table for a mode string ("drop2d"/"scanline"/"drop3d"/"circle"). Falls
// back to Drop2D.
const std::vector<ParticleSlotInfo>& particleSlotsForMode(const std::string& mode);

// Default effect name for a given (mode, slug). Mirrors the material
// `default_<mode>_<slug>` convention.
inline std::string defaultParticleEffectName(const std::string& mode,
                                             const std::string& slug) {
    return "default_" + mode + "_" + slug;
}
