#include "AIEditorConfig.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

// Minimal hand-rolled JSON readers matching PlayerSettings.cpp. The config
// has only four fields; pulling in nlohmann/json here would trade a lot of
// compile time for no real gain.

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

double jsonDouble(const std::string& json, const std::string& key, double def) {
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return def;
    return std::strtod(json.c_str() + pos + 1, nullptr);
}

} // namespace

bool loadAIEditorConfig(const std::string& path, AIEditorConfig& out) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::stringstream buf; buf << f.rdbuf();
    const std::string json = buf.str();
    if (json.empty()) return false;

    if (jsonHas(json, "endpoint"))    out.endpoint    = jsonString(json, "endpoint");
    if (jsonHas(json, "model"))       out.model       = jsonString(json, "model");
    if (jsonHas(json, "apiKey"))      out.apiKey      = jsonString(json, "apiKey");
    if (jsonHas(json, "timeoutSecs")) out.timeoutSecs = (int)jsonDouble(json, "timeoutSecs", out.timeoutSecs);
    if (out.endpoint.empty())   out.endpoint = "http://localhost:11434/v1";
    if (out.model.empty())      out.model    = "qwen2.5:3b";
    if (out.timeoutSecs <= 0)   out.timeoutSecs = 60;
    return true;
}

bool saveAIEditorConfig(const std::string& path, const AIEditorConfig& s) {
    std::ofstream f(path, std::ios::trunc);
    if (!f.is_open()) return false;
    char buf[2048];
    std::snprintf(buf, sizeof(buf),
        "{\n"
        "  \"endpoint\": \"%s\",\n"
        "  \"model\": \"%s\",\n"
        "  \"apiKey\": \"%s\",\n"
        "  \"timeoutSecs\": %d\n"
        "}\n",
        s.endpoint.c_str(),
        s.model.c_str(),
        s.apiKey.c_str(),
        s.timeoutSecs);
    f << buf;
    return f.good();
}
