#include "MaterialAsset.h"
#include <fstream>
#include <sstream>

namespace {

// Minimal hand-rolled JSON scanner — the engine already parses JSON this way
// in ChartLoader. Keeping the same style avoids pulling in an extra lib for
// what is ultimately a dozen fields.
std::string scanStringField(const std::string& src, const char* key) {
    std::string needle = std::string("\"") + key + "\"";
    auto kp = src.find(needle);
    if (kp == std::string::npos) return "";
    auto cp = src.find(':', kp);
    if (cp == std::string::npos) return "";
    auto qs = src.find('"', cp + 1);
    if (qs == std::string::npos) return "";
    auto qe = src.find('"', qs + 1);
    if (qe == std::string::npos) return "";
    return src.substr(qs + 1, qe - qs - 1);
}

bool hasField(const std::string& src, const char* key) {
    return src.find(std::string("\"") + key + "\"") != std::string::npos;
}

// Scan a scalar number `"key": <num>`. Returns `def` when absent/unparseable.
float scanFloat(const std::string& src, const char* key, float def) {
    std::string needle = std::string("\"") + key + "\"";
    auto kp = src.find(needle);
    if (kp == std::string::npos) return def;
    auto cp = src.find(':', kp);
    if (cp == std::string::npos) return def;
    size_t pos = cp + 1;
    while (pos < src.size() && (src[pos] == ' ' || src[pos] == '\t' ||
                                src[pos] == '\n' || src[pos] == '\r')) ++pos;
    size_t e = pos;
    while (e < src.size() && (src[e] == '-' || src[e] == '+' || src[e] == '.' ||
                              src[e] == 'e' || src[e] == 'E' ||
                              (src[e] >= '0' && src[e] <= '9'))) ++e;
    if (e == pos) return def;
    try { return std::stof(src.substr(pos, e - pos)); } catch (...) { return def; }
}

bool scanBool(const std::string& src, const char* key, bool def) {
    std::string needle = std::string("\"") + key + "\"";
    auto kp = src.find(needle);
    if (kp == std::string::npos) return def;
    auto cp = src.find(':', kp);
    if (cp == std::string::npos) return def;
    auto tp = src.find("true",  cp);
    auto fp = src.find("false", cp);
    // Pick whichever keyword comes first after the colon.
    if (tp != std::string::npos && (fp == std::string::npos || tp < fp)) return true;
    if (fp != std::string::npos) return false;
    return def;
}

template <size_t N>
void scanFloatArr(const std::string& src, const char* key,
                  std::array<float, N>& out, const std::array<float, N>& def) {
    out = def;
    std::string needle = std::string("\"") + key + "\"";
    auto kp = src.find(needle);
    if (kp == std::string::npos) return;
    auto ob = src.find('[', kp);
    if (ob == std::string::npos) return;
    auto oe = src.find(']', ob);
    if (oe == std::string::npos) return;
    std::string inner = src.substr(ob + 1, oe - ob - 1);
    size_t pos = 0;
    size_t idx = 0;
    while (pos < inner.size() && idx < N) {
        while (pos < inner.size() && (inner[pos] == ' ' || inner[pos] == ',' ||
                                      inner[pos] == '\t' || inner[pos] == '\n'))
            ++pos;
        if (pos >= inner.size()) break;
        size_t e = pos;
        while (e < inner.size() && inner[e] != ',' && inner[e] != ' ' &&
               inner[e] != '\t' && inner[e] != '\n')
            ++e;
        try { out[idx++] = std::stof(inner.substr(pos, e - pos)); }
        catch (...) {}
        pos = e;
    }
}

} // namespace

void migrateAssetV1toV2(MaterialAsset& a) {
    switch (a.kind) {
        case MaterialKind::Unlit:
            // Plain diffuse: take the tint as base color, fully rough,
            // non-metallic, no emission. Looks the same flat but now lit.
            a.cls               = MaterialClass::Pbr;
            a.baseColor         = a.tint;
            a.metallic          = 0.f;
            a.roughness         = 1.f;
            a.emissiveColor     = {0.f, 0.f, 0.f};
            a.emissiveIntensity = 0.f;
            a.useNormalMap      = false;
            a.baseColorTexPath  = a.texturePath;
            break;
        case MaterialKind::Glow:
            // Glow → emissive PBR. Legacy params[0] was the glow intensity.
            a.cls               = MaterialClass::Pbr;
            a.baseColor         = a.tint;
            a.metallic          = 0.f;
            a.roughness         = 0.5f;
            a.emissiveColor     = {a.tint[0], a.tint[1], a.tint[2]};
            a.emissiveIntensity = a.params[0] > 0.f ? a.params[0] : 1.f;
            a.useNormalMap      = false;
            a.baseColorTexPath  = a.texturePath;
            break;
        default:
            // Scroll/Pulse/Gradient/Custom keep working as special effects.
            a.cls = MaterialClass::SpecialEffect;
            break;
    }
}

bool saveMaterialAsset(const MaterialAsset& asset, const std::filesystem::path& savePath) {
    std::ofstream f(savePath);
    if (!f.is_open()) return false;

    f << "{\n";
    f << "  \"version\": 2,\n";
    f << "  \"class\": \"" << classToString(asset.cls) << "\",\n";
    f << "  \"name\": \""  << asset.name << "\"";

    if (asset.cls == MaterialClass::Pbr) {
        f << ",\n  \"baseColor\": [" << asset.baseColor[0] << ", " << asset.baseColor[1]
          << ", " << asset.baseColor[2] << ", " << asset.baseColor[3] << "]";
        f << ",\n  \"metallic\": "  << asset.metallic;
        f << ",\n  \"roughness\": " << asset.roughness;
        f << ",\n  \"emissiveColor\": [" << asset.emissiveColor[0] << ", "
          << asset.emissiveColor[1] << ", " << asset.emissiveColor[2] << "]";
        f << ",\n  \"emissiveIntensity\": " << asset.emissiveIntensity;
        f << ",\n  \"useNormalMap\": " << (asset.useNormalMap ? "true" : "false");
        if (!asset.baseColorTexPath.empty())
            f << ",\n  \"baseColorTexture\": \"" << asset.baseColorTexPath << "\"";
        if (!asset.normalTexPath.empty())
            f << ",\n  \"normalTexture\": \"" << asset.normalTexPath << "\"";
    } else {
        f << ",\n  \"kind\": \"" << kindName(asset.kind) << "\"";
        f << ",\n  \"tint\": ["  << asset.tint[0] << ", " << asset.tint[1]
          << ", " << asset.tint[2] << ", " << asset.tint[3] << "]";
        f << ",\n  \"params\": [" << asset.params[0] << ", " << asset.params[1]
          << ", " << asset.params[2] << ", " << asset.params[3] << "]";
        if (!asset.texturePath.empty())
            f << ",\n  \"texture\": \"" << asset.texturePath << "\"";
        if (!asset.customShaderPath.empty())
            f << ",\n  \"shader\": \"" << asset.customShaderPath << "\"";
    }

    if (!asset.targetMode.empty())
        f << ",\n  \"targetMode\": \"" << asset.targetMode << "\"";
    if (!asset.targetSlotSlug.empty())
        f << ",\n  \"targetSlot\": \"" << asset.targetSlotSlug << "\"";
    f << "\n}\n";
    return true;
}

bool loadMaterialAsset(const std::filesystem::path& loadPath, MaterialAsset& out) {
    std::ifstream f(loadPath);
    if (!f.is_open()) return false;
    std::stringstream buf;
    buf << f.rdbuf();
    std::string src = buf.str();

    out.name = scanStringField(src, "name");
    if (out.name.empty()) out.name = loadPath.stem().string();

    out.targetMode     = scanStringField(src, "targetMode");
    out.targetSlotSlug = scanStringField(src, "targetSlot");

    bool isV2 = hasField(src, "version") || hasField(src, "class");

    if (isV2) {
        out.wasLegacyV1 = false;
        out.cls = parseClass(scanStringField(src, "class"));
        if (out.cls == MaterialClass::Pbr) {
            scanFloatArr<4>(src, "baseColor", out.baseColor, {1.f, 1.f, 1.f, 1.f});
            out.metallic          = scanFloat(src, "metallic", 0.f);
            out.roughness         = scanFloat(src, "roughness", 0.5f);
            scanFloatArr<3>(src, "emissiveColor", out.emissiveColor, {0.f, 0.f, 0.f});
            out.emissiveIntensity = scanFloat(src, "emissiveIntensity", 0.f);
            out.useNormalMap      = scanBool(src, "useNormalMap", false);
            out.baseColorTexPath  = scanStringField(src, "baseColorTexture");
            out.normalTexPath     = scanStringField(src, "normalTexture");
        } else {
            out.kind = parseKind(scanStringField(src, "kind"));
            scanFloatArr<4>(src, "tint",   out.tint,   {1.f, 1.f, 1.f, 1.f});
            scanFloatArr<4>(src, "params", out.params, {0.f, 0.f, 0.f, 0.f});
            out.texturePath      = scanStringField(src, "texture");
            out.customShaderPath = scanStringField(src, "shader");
        }
        return true;
    }

    // ── Legacy v1: flat kind/tint/params → migrate in memory ────────────────
    out.kind = parseKind(scanStringField(src, "kind"));
    scanFloatArr<4>(src, "tint",   out.tint,   {1.f, 1.f, 1.f, 1.f});
    scanFloatArr<4>(src, "params", out.params, {0.f, 0.f, 0.f, 0.f});
    out.texturePath      = scanStringField(src, "texture");
    out.customShaderPath = scanStringField(src, "shader");
    migrateAssetV1toV2(out);
    out.wasLegacyV1 = true;
    return true;
}
