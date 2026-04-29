#include "editor/CopilotSkill.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>

namespace {

namespace fs = std::filesystem;

// CWD is anchored to the exe directory at startup (see main.cpp).
// Skill files are copied to <exe_dir>/assets/copilot_skills/ by the
// build. Also probe a few likely dev paths so running from a worktree
// without a full rebuild still works.
std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string tryReadSkill(const std::string& stem) {
    const fs::path candidates[] = {
        fs::path("assets") / "copilot_skills" / (stem + ".md"),
        fs::path("..") / "assets" / "copilot_skills" / (stem + ".md"),
        fs::path("engine") / "assets" / "copilot_skills" / (stem + ".md"),
        fs::path("../..") / "engine" / "assets" / "copilot_skills" / (stem + ".md"),
    };
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::exists(c, ec)) {
            std::string body = readFile(c);
            if (!body.empty()) return body;
        }
    }
    return {};
}

// Warn once per mode when its skill file is missing so we don't spam the
// log on every request.
std::unordered_set<std::string>& warnedModes() {
    static std::unordered_set<std::string> s;
    return s;
}

} // namespace

std::string loadCopilotSkill(const std::string& modeName) {
    std::string common = tryReadSkill("_common");
    std::string mode   = tryReadSkill(modeName);

    if (mode.empty()) {
        if (warnedModes().insert(modeName).second) {
            std::cerr << "[copilot] skill file for mode '" << modeName
                      << "' not found under assets/copilot_skills/; "
                         "falling back to inline prompt\n";
        }
        return {};
    }

    if (common.empty()) return mode;
    return common + "\n\n" + mode;
}
