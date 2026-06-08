#pragma once
#include <string>
#include <vector>
#include "game/chart/ChartTypes.h"   // NoteType

class AudioEngine;

// Built-in sound-effect LIBRARY shipped with the engine, plus the per-role
// fallback defaults. The library is a browsable pool (short one-shots + long
// loops) that game devs pick from in the editor; when nothing is picked, each
// engine role falls back to a default library entry so the game is never silent.
//
// Library contents and the role->default mapping come from sfx/manifest.json in
// the bundle dir (desktop: relative to the exe; Android: the extracted internal
// path set via setBundleDir). A missing/partial manifest degrades to silence
// (no crash). Charts store a bundled pick as "builtin:<file>" and a dev's own
// imported file as a project-relative path.
namespace DefaultSfx {

enum class Role {
    Click, Flick, HoldStart, HoldLoop, HoldTick, HoldEnd,
    UiTap, UiConfirm, UiScroll, UiBack, UiToggle
};

enum class Category { Short, Long };

struct LibEntry {
    std::string file;   // bundle-relative, e.g. "short/click.wav"
    std::string name;   // display name for the picker
};

// Stable AudioEngine cache key for a role's one-shot, e.g. "def_click".
const char* cacheKeyForRole(Role role);

// Bundle directory. Desktop default is the relative "sfx" (CWD is the exe dir);
// Android sets the extracted internal path via setBundleDir (which also forces
// the manifest to reload from the new location).
void        setBundleDir(const std::string& dir);
std::string bundleDir();

// Library browsing (for the editor picker). Loaded lazily from manifest.json.
const std::vector<LibEntry>& library(Category cat);
// All bundle-relative library files (short + long) — used by Android to know
// which assets to extract from the APK.
std::vector<std::string> allLibraryFiles();

// Resolve a chart sfx reference to a playable absolute/relative path:
//   ""                        -> "" (caller uses the role default cache key)
//   "builtin:short/click.wav" -> <bundleDir>/short/click.wav
//   "assets/audio/x.mp3"      -> <projectDir>/assets/audio/x.mp3
//   absolute path             -> unchanged
std::string resolveRef(const std::string& ref, const std::string& projectDir);

// Bundle-relative default file for a role (from manifest "defaults"), or "".
std::string defaultFileForRole(Role role);
// Full path to a role's default file (bundleDir + default), or "" if none.
std::string pathForRole(Role role);

// Preload every one-shot role's default into the AudioEngine cache under its
// "def_*" key. Idempotent and tolerant of missing files. The HoldLoop default
// is not cached (it is played via AudioEngine::startLoopingSfx by path).
void preloadDefaults(AudioEngine& audio);

// Map a note hit to its default role (isHoldEnd selects the release sound).
Role roleForNoteHit(NoteType type, bool isHoldEnd);
// The GameModeConfig::noteAssets section key for a note type
// ("Click Note" / "Hold Note" / "Flick Note" / "Slide Note" / "Arc Note" /
// "ArcTap Note") — used to look up a dev-supplied per-note override.
const char* sectionKeyForNoteType(NoteType type);

} // namespace DefaultSfx
