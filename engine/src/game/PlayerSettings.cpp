#include "PlayerSettings.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string jsonString(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + needle.size());
    if (pos == std::string::npos) return "";
    ++pos;
    std::string result;
    for (size_t i = pos; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) { result += json[++i]; continue; }
        if (json[i] == '"') break;
        result += json[i];
    }
    return result;
}

bool jsonHas(const std::string& json, const std::string& key) {
    return json.find("\"" + key + "\"") != std::string::npos;
}

double jsonDouble(const std::string& json, const std::string& key, double defaultVal) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return defaultVal;
    return std::strtod(json.c_str() + pos + 1, nullptr);
}

bool jsonBool(const std::string& json, const std::string& key, bool defaultVal) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return defaultVal;
    while (pos < json.size() && (json[pos] == ':' || json[pos] == ' ')) ++pos;
    if (pos >= json.size()) return defaultVal;
    return json.compare(pos, 4, "true") == 0;
}

} // namespace

bool loadPlayerSettings(const std::string& path, PlayerSettings& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::stringstream buf; buf << f.rdbuf();
    const std::string json = buf.str();
    if (json.empty()) return false;

    if (jsonHas(json, "musicVolume"))     out.musicVolume     = static_cast<float>(jsonDouble(json, "musicVolume", out.musicVolume));
    if (jsonHas(json, "hitSoundVolume"))  out.hitSoundVolume  = static_cast<float>(jsonDouble(json, "hitSoundVolume", out.hitSoundVolume));
    if (jsonHas(json, "hitSoundEnabled")) out.hitSoundEnabled = jsonBool  (json, "hitSoundEnabled", out.hitSoundEnabled);
    if (jsonHas(json, "audioOffsetMs"))   out.audioOffsetMs   = static_cast<float>(jsonDouble(json, "audioOffsetMs", out.audioOffsetMs));
    if (jsonHas(json, "noteSpeed"))       out.noteSpeed       = static_cast<float>(jsonDouble(json, "noteSpeed", out.noteSpeed));
    if (jsonHas(json, "backgroundDim"))   out.backgroundDim   = static_cast<float>(jsonDouble(json, "backgroundDim", out.backgroundDim));
    if (jsonHas(json, "fpsCounter"))      out.fpsCounter      = jsonBool  (json, "fpsCounter", out.fpsCounter);
    if (jsonHas(json, "language"))        out.language        = jsonString(json, "language");
    if (out.language.empty()) out.language = "en";
    return true;
}

bool savePlayerSettings(const std::string& path, const PlayerSettings& s) {
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return false;
    char buf[1024];
    std::snprintf(buf, sizeof(buf),
        "{\n"
        "  \"musicVolume\": %.4f,\n"
        "  \"hitSoundVolume\": %.4f,\n"
        "  \"hitSoundEnabled\": %s,\n"
        "  \"audioOffsetMs\": %.3f,\n"
        "  \"noteSpeed\": %.3f,\n"
        "  \"backgroundDim\": %.3f,\n"
        "  \"fpsCounter\": %s,\n"
        "  \"language\": \"%s\"\n"
        "}\n",
        s.musicVolume,
        s.hitSoundVolume,
        s.hitSoundEnabled ? "true" : "false",
        s.audioOffsetMs,
        s.noteSpeed,
        s.backgroundDim,
        s.fpsCounter ? "true" : "false",
        s.language.c_str());
    f << buf;
    return f.good();
}
