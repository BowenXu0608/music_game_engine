#include "DefaultSfx.h"
#include "AudioEngine.h"

#include <nlohmann/json.hpp>
#include <fstream>

namespace DefaultSfx {

namespace {

std::string g_bundleDir = "sfx";   // desktop default; CWD is the exe dir

struct Manifest {
    bool loaded = false;
    std::vector<LibEntry> shortEntries;
    std::vector<LibEntry> longEntries;
    // role string -> bundle-relative default file
    std::vector<std::pair<std::string, std::string>> defaults;
};
Manifest g_manifest;

const char* roleKey(Role role) {
    switch (role) {
        case Role::Click:     return "click";
        case Role::Flick:     return "flick";
        case Role::HoldStart: return "hold_start";
        case Role::HoldLoop:  return "hold_loop";
        case Role::HoldTick:  return "hold_tick";
        case Role::HoldEnd:   return "hold_end";
        case Role::UiTap:     return "ui_tap";
        case Role::UiConfirm: return "ui_confirm";
        case Role::UiScroll:  return "ui_scroll";
        case Role::UiBack:    return "ui_back";
        case Role::UiToggle:  return "ui_toggle";
    }
    return "click";
}

void ensureLoaded() {
    if (g_manifest.loaded) return;
    g_manifest.loaded = true;   // mark first so a parse failure won't retry-spam
    g_manifest.shortEntries.clear();
    g_manifest.longEntries.clear();
    g_manifest.defaults.clear();

    std::ifstream f(g_bundleDir + "/manifest.json");
    if (!f.is_open()) return;
    nlohmann::json j;
    try {
        f >> j;
    } catch (...) {
        return;   // malformed manifest -> empty library (silent), no crash
    }

    auto readList = [](const nlohmann::json& arr, std::vector<LibEntry>& out) {
        if (!arr.is_array()) return;
        for (const auto& e : arr) {
            if (!e.is_object()) continue;
            LibEntry le;
            le.file = e.value("file", "");
            le.name = e.value("name", le.file);
            if (!le.file.empty()) out.push_back(std::move(le));
        }
    };
    if (j.contains("short")) readList(j["short"], g_manifest.shortEntries);
    if (j.contains("long"))  readList(j["long"],  g_manifest.longEntries);

    if (j.contains("defaults") && j["defaults"].is_object()) {
        for (auto it = j["defaults"].begin(); it != j["defaults"].end(); ++it) {
            if (it.value().is_string())
                g_manifest.defaults.emplace_back(it.key(), it.value().get<std::string>());
        }
    }
}

} // namespace

const char* cacheKeyForRole(Role role) {
    switch (role) {
        case Role::Click:     return "def_click";
        case Role::Flick:     return "def_flick";
        case Role::HoldStart: return "def_hold_start";
        case Role::HoldLoop:  return "def_hold_loop";
        case Role::HoldTick:  return "def_hold_tick";
        case Role::HoldEnd:   return "def_hold_end";
        case Role::UiTap:     return "def_ui_tap";
        case Role::UiConfirm: return "def_ui_confirm";
        case Role::UiScroll:  return "def_ui_scroll";
        case Role::UiBack:    return "def_ui_back";
        case Role::UiToggle:  return "def_ui_toggle";
    }
    return "def_click";
}

void setBundleDir(const std::string& dir) {
    g_bundleDir = dir;
    g_manifest.loaded = false;   // reload from the new location on next access
}
std::string bundleDir() { return g_bundleDir; }

const std::vector<LibEntry>& library(Category cat) {
    ensureLoaded();
    return cat == Category::Long ? g_manifest.longEntries : g_manifest.shortEntries;
}

std::vector<std::string> allLibraryFiles() {
    ensureLoaded();
    std::vector<std::string> out;
    for (const auto& e : g_manifest.shortEntries) out.push_back(e.file);
    for (const auto& e : g_manifest.longEntries)  out.push_back(e.file);
    return out;
}

std::string resolveRef(const std::string& ref, const std::string& projectDir) {
    if (ref.empty()) return "";
    if (ref.rfind("builtin:", 0) == 0)
        return g_bundleDir + "/" + ref.substr(8);
    const bool isAbs = ref.size() >= 2 && (ref[1] == ':' || ref[0] == '/' || ref[0] == '\\');
    if (isAbs) return ref;
    if (!projectDir.empty()) return projectDir + "/" + ref;
    return ref;
}

std::string defaultFileForRole(Role role) {
    ensureLoaded();
    const char* key = roleKey(role);
    for (const auto& [k, file] : g_manifest.defaults)
        if (k == key) return file;
    return "";
}

std::string pathForRole(Role role) {
    std::string file = defaultFileForRole(role);
    if (file.empty()) return "";
    return g_bundleDir + "/" + file;
}

void preloadDefaults(AudioEngine& audio) {
    const Role oneShots[] = {
        Role::Click, Role::Flick, Role::HoldStart, Role::HoldTick, Role::HoldEnd,
        Role::UiTap, Role::UiConfirm, Role::UiScroll, Role::UiBack, Role::UiToggle
    };
    for (Role r : oneShots) {
        std::string p = pathForRole(r);
        if (!p.empty())
            audio.preloadSfx(cacheKeyForRole(r), p);
    }
}

Role roleForNoteHit(NoteType type, bool isHoldEnd) {
    if (isHoldEnd) return Role::HoldEnd;
    switch (type) {
        case NoteType::Flick:  return Role::Flick;
        case NoteType::Hold:   return Role::HoldStart;
        case NoteType::Slide:  return Role::HoldStart;
        default:               return Role::Click;   // Tap/Drag/Arc/ArcTap/Ring
    }
}

const char* sectionKeyForNoteType(NoteType type) {
    switch (type) {
        case NoteType::Hold:   return "Hold Note";
        case NoteType::Flick:  return "Flick Note";
        case NoteType::Slide:  return "Slide Note";
        case NoteType::Arc:    return "Arc Note";
        case NoteType::ArcTap: return "ArcTap Note";
        default:               return "Click Note";  // Tap/Drag/Ring
    }
}

} // namespace DefaultSfx
