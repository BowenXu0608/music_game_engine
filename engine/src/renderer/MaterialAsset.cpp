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

void scanFloat4(const std::string& src, const char* key,
                std::array<float, 4>& out,
                float d0, float d1, float d2, float d3) {
    out = {d0, d1, d2, d3};
    std::string needle = std::string("\"") + key + "\"";
    auto kp = src.find(needle);
    if (kp == std::string::npos) return;
    auto ob = src.find('[', kp);
    if (ob == std::string::npos) return;
    auto oe = src.find(']', ob);
    if (oe == std::string::npos) return;
    std::string inner = src.substr(ob + 1, oe - ob - 1);
    size_t pos = 0;
    int idx = 0;
    while (pos < inner.size() && idx < 4) {
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

bool saveMaterialAsset(const MaterialAsset& asset, const std::filesystem::path& savePath) {
    std::ofstream f(savePath);
    if (!f.is_open()) return false;

    f << "{\n";
    f << "  \"name\": \""  << asset.name                  << "\",\n";
    f << "  \"kind\": \""  << kindName(asset.kind)        << "\",\n";
    f << "  \"tint\": ["   << asset.tint[0]   << ", " << asset.tint[1]
                    << ", " << asset.tint[2]   << ", " << asset.tint[3]   << "],\n";
    f << "  \"params\": [" << asset.params[0] << ", " << asset.params[1]
                    << ", " << asset.params[2] << ", " << asset.params[3] << "]";
    if (!asset.texturePath.empty())
        f << ",\n  \"texture\": \"" << asset.texturePath << "\"";
    if (!asset.customShaderPath.empty())
        f << ",\n  \"shader\": \""  << asset.customShaderPath << "\"";
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

    out.name             = scanStringField(src, "name");
    // Fall back to the file stem if "name" is missing — keeps the library
    // resolvable even for hand-edited files that forgot the field.
    if (out.name.empty()) out.name = loadPath.stem().string();

    std::string kind     = scanStringField(src, "kind");
    out.kind             = parseKind(kind);
    scanFloat4(src, "tint",   out.tint,   1.f, 1.f, 1.f, 1.f);
    scanFloat4(src, "params", out.params, 0.f, 0.f, 0.f, 0.f);
    out.texturePath      = scanStringField(src, "texture");
    out.customShaderPath = scanStringField(src, "shader");
    out.targetMode       = scanStringField(src, "targetMode");
    out.targetSlotSlug   = scanStringField(src, "targetSlot");
    return true;
}
