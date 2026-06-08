#include "ParticleEffectAsset.h"
#include "MaterialSlots.h"
#include <fstream>
#include <sstream>

namespace {

// Same hand-rolled JSON scanner style as MaterialAsset.cpp.
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

int scanInt(const std::string& src, const char* key, int def) {
    return static_cast<int>(scanFloat(src, key, static_cast<float>(def)));
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
    size_t pos = 0, idx = 0;
    while (pos < inner.size() && idx < N) {
        while (pos < inner.size() && (inner[pos] == ' ' || inner[pos] == ',' ||
                                      inner[pos] == '\t' || inner[pos] == '\n')) ++pos;
        if (pos >= inner.size()) break;
        size_t e = pos;
        while (e < inner.size() && inner[e] != ',' && inner[e] != ' ' &&
               inner[e] != '\t' && inner[e] != '\n') ++e;
        try { out[idx++] = std::stof(inner.substr(pos, e - pos)); } catch (...) {}
        pos = e;
    }
}

} // namespace

const char* particleKindName(ParticleEffectKind k) {
    switch (k) {
        case ParticleEffectKind::Burst:  return "burst";
        case ParticleEffectKind::Spark:  return "spark";
        case ParticleEffectKind::Ring:   return "ring";
        case ParticleEffectKind::Aura:   return "aura";
        case ParticleEffectKind::Custom: return "custom";
    }
    return "burst";
}

ParticleEffectKind parseParticleKind(const std::string& s) {
    if (s == "spark")  return ParticleEffectKind::Spark;
    if (s == "ring")   return ParticleEffectKind::Ring;
    if (s == "aura")   return ParticleEffectKind::Aura;
    if (s == "custom") return ParticleEffectKind::Custom;
    return ParticleEffectKind::Burst;
}

ParticleEmit particleEmitFromAsset(const ParticleEffectAsset& a, uint16_t pipeKey) {
    ParticleEmit fx{};
    switch (a.kind) {
        case ParticleEffectKind::Spark:  fx.pattern = ParticleEmit::Pattern::Directional; break;
        case ParticleEffectKind::Ring:   fx.pattern = ParticleEmit::Pattern::Ring;        break;
        case ParticleEffectKind::Aura:   fx.pattern = ParticleEmit::Pattern::Aura;        break;
        case ParticleEffectKind::Burst:
        case ParticleEffectKind::Custom: fx.pattern = ParticleEmit::Pattern::Radial;      break;
    }
    fx.count     = a.count;
    fx.speedMin  = a.speedMin;
    fx.speedMax  = a.speedMax;
    fx.sizeStart = a.sizeStart;
    fx.sizeEnd   = a.sizeEnd;
    fx.lifeMin   = a.lifeMin;
    fx.lifeMax   = a.lifeMax;
    fx.spread    = a.spread;
    fx.gravity   = {a.gravity[0], a.gravity[1]};
    fx.drag      = a.drag;
    fx.rateHz    = a.rateHz;
    fx.color     = {a.color[0],    a.color[1],    a.color[2],    a.color[3]};
    fx.colorEnd  = {a.colorEnd[0], a.colorEnd[1], a.colorEnd[2], a.colorEnd[3]};
    fx.pipeKey   = pipeKey;
    return fx;
}

bool saveParticleEffectAsset(const ParticleEffectAsset& a,
                             const std::filesystem::path& savePath) {
    std::ofstream f(savePath);
    if (!f.is_open()) return false;
    f << "{\n";
    f << "  \"version\": 1,\n";
    f << "  \"name\": \""  << a.name << "\",\n";
    f << "  \"kind\": \""  << particleKindName(a.kind) << "\",\n";
    f << "  \"count\": "    << a.count    << ",\n";
    f << "  \"speedMin\": " << a.speedMin << ",\n";
    f << "  \"speedMax\": " << a.speedMax << ",\n";
    f << "  \"sizeStart\": "<< a.sizeStart<< ",\n";
    f << "  \"sizeEnd\": "  << a.sizeEnd  << ",\n";
    f << "  \"lifeMin\": "  << a.lifeMin  << ",\n";
    f << "  \"lifeMax\": "  << a.lifeMax  << ",\n";
    f << "  \"spread\": "   << a.spread   << ",\n";
    f << "  \"gravity\": [" << a.gravity[0] << ", " << a.gravity[1] << "],\n";
    f << "  \"drag\": "     << a.drag     << ",\n";
    f << "  \"rateHz\": "   << a.rateHz   << ",\n";
    f << "  \"color\": ["   << a.color[0]    << ", " << a.color[1]    << ", "
                            << a.color[2]    << ", " << a.color[3]    << "],\n";
    f << "  \"colorEnd\": ["<< a.colorEnd[0] << ", " << a.colorEnd[1] << ", "
                            << a.colorEnd[2] << ", " << a.colorEnd[3] << "]";
    if (!a.customShaderPath.empty())
        f << ",\n  \"shader\": \"" << a.customShaderPath << "\"";
    if (!a.targetMode.empty())
        f << ",\n  \"targetMode\": \"" << a.targetMode << "\"";
    if (!a.targetSlotSlug.empty())
        f << ",\n  \"targetSlot\": \"" << a.targetSlotSlug << "\"";
    f << "\n}\n";
    return true;
}

bool loadParticleEffectAsset(const std::filesystem::path& loadPath,
                             ParticleEffectAsset& out) {
    std::ifstream f(loadPath);
    if (!f.is_open()) return false;
    std::stringstream buf;
    buf << f.rdbuf();
    std::string src = buf.str();

    out.name = scanStringField(src, "name");
    if (out.name.empty()) out.name = loadPath.stem().string();

    out.kind     = parseParticleKind(scanStringField(src, "kind"));
    out.count    = scanInt(src, "count", out.count);
    out.speedMin = scanFloat(src, "speedMin", out.speedMin);
    out.speedMax = scanFloat(src, "speedMax", out.speedMax);
    out.sizeStart= scanFloat(src, "sizeStart", out.sizeStart);
    out.sizeEnd  = scanFloat(src, "sizeEnd", out.sizeEnd);
    out.lifeMin  = scanFloat(src, "lifeMin", out.lifeMin);
    out.lifeMax  = scanFloat(src, "lifeMax", out.lifeMax);
    out.spread   = scanFloat(src, "spread", out.spread);
    scanFloatArr<2>(src, "gravity", out.gravity, {0.f, 0.f});
    out.drag     = scanFloat(src, "drag", out.drag);
    out.rateHz   = scanFloat(src, "rateHz", out.rateHz);
    scanFloatArr<4>(src, "color",    out.color,    {0.3f, 1.f, 0.5f, 1.f});
    scanFloatArr<4>(src, "colorEnd", out.colorEnd, {0.3f, 1.f, 0.5f, 0.f});
    out.customShaderPath = scanStringField(src, "shader");
    out.targetMode       = normalizeModeToken(scanStringField(src, "targetMode"));
    out.targetSlotSlug   = scanStringField(src, "targetSlot");
    return true;
}
