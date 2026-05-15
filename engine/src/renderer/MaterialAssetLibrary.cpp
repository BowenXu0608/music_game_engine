#include "MaterialAssetLibrary.h"
#include <algorithm>
#include <system_error>

namespace fs = std::filesystem;

std::filesystem::path MaterialAssetLibrary::materialsDir() const {
    return m_projectDir / "assets" / "materials";
}

void MaterialAssetLibrary::loadFromProject(const fs::path& projectDir) {
    m_projectDir = projectDir;
    m_assets.clear();

    fs::path dir = materialsDir();
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;

    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".mat") continue;

        MaterialAsset asset;
        if (!loadMaterialAsset(entry.path(), asset)) continue;
        if (asset.name.empty()) continue;
        m_assets[asset.name] = asset;
    }
}

void MaterialAssetLibrary::clear() {
    m_assets.clear();
    m_projectDir.clear();
}

const MaterialAsset* MaterialAssetLibrary::get(const std::string& name) const {
    auto it = m_assets.find(name);
    if (it == m_assets.end()) return nullptr;
    return &it->second;
}

bool MaterialAssetLibrary::upsert(const MaterialAsset& asset) {
    if (asset.name.empty()) return false;

    std::error_code ec;
    fs::create_directories(materialsDir(), ec);

    fs::path p = materialsDir() / (asset.name + ".mat");
    if (!saveMaterialAsset(asset, p)) return false;

    m_assets[asset.name] = asset;
    return true;
}

void MaterialAssetLibrary::remove(const std::string& name) {
    auto it = m_assets.find(name);
    if (it == m_assets.end()) return;
    m_assets.erase(it);

    std::error_code ec;
    fs::remove(materialsDir() / (name + ".mat"), ec);
}

std::vector<std::string> MaterialAssetLibrary::allNames() const {
    std::vector<std::string> out;
    out.reserve(m_assets.size());
    for (auto& [name, _] : m_assets) out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> MaterialAssetLibrary::namesCompatibleWith(
    MaterialModeKey mode, const std::string& slug) const
{
    std::string modeName = materialModeName(mode);
    std::vector<std::string> out;
    for (auto& [name, a] : m_assets) {
        if (!a.targetMode.empty()     && a.targetMode     != modeName) continue;
        if (!a.targetSlotSlug.empty() && a.targetSlotSlug != slug)     continue;
        out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

void MaterialAssetLibrary::seedDefaultMaterials(MaterialModeKey mode) {
    const auto& slots = getMaterialSlotsForMode(mode);
    const char* modeName = materialModeName(mode);
    for (const auto& slot : slots) {
        std::string slug = materialSlotSlug(slot);
        std::string name = std::string("default_") + modeName + "_" + slug;
        auto it = m_assets.find(name);
        if (it != m_assets.end()) {
            // Asset exists from an earlier run. Preserve user edits, but
            // backfill target fields if empty (older .mat files pre-date the
            // targetMode/targetSlotSlug additions) and rewrite v1 files as v2
            // so the in-memory migration persists to disk.
            bool dirty = it->second.wasLegacyV1;
            if (it->second.targetMode.empty())     { it->second.targetMode     = modeName; dirty = true; }
            if (it->second.targetSlotSlug.empty()) { it->second.targetSlotSlug = slug;     dirty = true; }
            if (dirty) { it->second.wasLegacyV1 = false; upsert(it->second); }
            continue;
        }
        // New default: start from the slot's legacy kind/tint/params and run
        // the same v1→v2 mapping, so Unlit slots become plain PBR and Glow
        // slots become emissive PBR. Effect-kind slots stay SpecialEffect.
        MaterialAsset asset;
        asset.name           = name;
        asset.kind           = slot.defaultKind;
        asset.tint           = {slot.defaultTint[0],   slot.defaultTint[1],
                                slot.defaultTint[2],   slot.defaultTint[3]};
        asset.params         = {slot.defaultParams[0], slot.defaultParams[1],
                                slot.defaultParams[2], slot.defaultParams[3]};
        migrateAssetV1toV2(asset);
        asset.targetMode     = modeName;
        asset.targetSlotSlug = slug;
        upsert(asset);
    }
}

namespace {
// Byte-level equality between a chart's inline material entry and the slot's
// built-in default. Exact float compare — JSON floats round-trip losslessly
// in practice (trailing digits stay stable), so this is only a false
// negative if the chart was hand-edited with weird precision.
bool inlineMatchesDefault(const ChartData::MaterialData& md,
                          const MaterialSlotInfo& slot) {
    // Kind comparison: legacy `md.kind` is a string; slot default is an enum.
    if (parseKind(md.kind) != slot.defaultKind) return false;
    for (int i = 0; i < 4; ++i) {
        if (md.tint[i]   != slot.defaultTint[i])   return false;
        if (md.params[i] != slot.defaultParams[i]) return false;
    }
    if (!md.texturePath.empty()) return false;   // defaults carry no texture
    return true;
}
} // namespace

void MaterialAssetLibrary::migrateChartToAssets(ChartData& chart,
                                                const std::string& chartStem,
                                                MaterialModeKey mode) {
    const auto& slots = getMaterialSlotsForMode(mode);
    const char* modeName = materialModeName(mode);

    // Build slot-id → info lookup up-front.
    std::unordered_map<uint16_t, const MaterialSlotInfo*> slotById;
    for (const auto& s : slots) slotById[s.id] = &s;

    for (auto& md : chart.materials) {
        if (!md.assetName.empty()) continue;   // already references an asset

        auto it = slotById.find(md.slot);
        if (it == slotById.end()) {
            // Unknown slot — fall back to the old cryptic naming so the
            // reference still resolves to something.
            std::string name = chartStem + "_" + std::to_string((int)md.slot);
            MaterialAsset a;
            a.name   = name;
            a.kind   = parseKind(md.kind);
            a.tint   = {md.tint[0], md.tint[1], md.tint[2], md.tint[3]};
            a.params = {md.params[0], md.params[1], md.params[2], md.params[3]};
            a.texturePath = md.texturePath;
            migrateAssetV1toV2(a);
            upsert(a);
            md.assetName = name;
            continue;
        }

        const MaterialSlotInfo& slot = *it->second;
        std::string slug = materialSlotSlug(slot);

        if (inlineMatchesDefault(md, slot)) {
            // Chart's override matches the mode's built-in default → point
            // at the shared default asset instead of creating a per-chart
            // copy. Shared defaults were pre-seeded by seedDefaultMaterials.
            md.assetName = std::string("default_") + modeName + "_" + slug;
        } else {
            // Per-chart override. Double-underscore separator between the
            // chart stem and slug makes the origin readable at a glance.
            std::string name = chartStem + "__" + slug;
            MaterialAsset a;
            a.name           = name;
            a.kind           = parseKind(md.kind);
            a.tint           = {md.tint[0], md.tint[1], md.tint[2], md.tint[3]};
            a.params         = {md.params[0], md.params[1], md.params[2], md.params[3]};
            a.texturePath    = md.texturePath;
            migrateAssetV1toV2(a);
            a.targetMode     = modeName;
            a.targetSlotSlug = slug;
            upsert(a);
            md.assetName = name;
        }
    }
}

void MaterialAssetLibrary::pruneOldCrypticFiles(const std::vector<std::string>& chartStems) {
    // Old Phase-A scheme named per-chart overrides as `<chartStem>_<digits>.mat`.
    // Only remove files that (a) match that literal pattern AND (b) whose
    // prefix is one of the known chart stems in this project, so unrelated
    // user-created assets like `effect_v1` are never touched.
    std::error_code ec;
    if (!std::filesystem::is_directory(materialsDir(), ec)) return;
    std::vector<std::filesystem::path> doomed;
    for (auto& entry : std::filesystem::directory_iterator(materialsDir(), ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".mat") continue;
        std::string stem = entry.path().stem().string();

        for (const auto& chartStem : chartStems) {
            // Must start with `<chartStem>_` then only digits (not `__<slug>`).
            if (stem.size() <= chartStem.size() + 1) continue;
            if (stem.compare(0, chartStem.size(), chartStem) != 0) continue;
            if (stem[chartStem.size()] != '_') continue;
            if (stem.size() > chartStem.size() + 1 &&
                stem[chartStem.size() + 1] == '_') continue;   // new "__<slug>" form
            std::string tail = stem.substr(chartStem.size() + 1);
            bool allDigits = !tail.empty();
            for (char c : tail) if (c < '0' || c > '9') { allDigits = false; break; }
            if (allDigits) {
                doomed.push_back(entry.path());
                break;
            }
        }
    }
    for (auto& p : doomed) {
        m_assets.erase(p.stem().string());
        std::filesystem::remove(p, ec);
    }
}

namespace {
// Expand a project-relative path to absolute against the library's project
// root. Empty stays empty.
std::string toAbs(const MaterialAssetLibrary* lib, const std::string& rel) {
    if (rel.empty() || !lib) return rel;
    return (lib->projectDir() / rel).string();
}

void fillFromAsset(Material& m, const MaterialAsset& a,
                   const MaterialAssetLibrary* lib) {
    m.cls = a.cls;
    if (a.cls == MaterialClass::Pbr) {
        m.pbr.baseColor         = {a.baseColor[0], a.baseColor[1],
                                   a.baseColor[2], a.baseColor[3]};
        m.pbr.metallic          = a.metallic;
        m.pbr.roughness         = a.roughness;
        m.pbr.emissiveColor     = {a.emissiveColor[0], a.emissiveColor[1],
                                   a.emissiveColor[2]};
        m.pbr.emissiveIntensity = a.emissiveIntensity;
        bool hasNormal = a.useNormalMap && !a.normalTexPath.empty();
        m.pbr.flags = hasNormal ? PBR_FLAG_HAS_NORMAL_MAP : 0.f;
        m.baseColorTexPath = toAbs(lib, a.baseColorTexPath);
        m.normalTexPath    = hasNormal ? toAbs(lib, a.normalTexPath) : "";
    } else {
        m.kind   = a.kind;
        m.tint   = {a.tint[0],   a.tint[1],   a.tint[2],   a.tint[3]};
        m.params = {a.params[0], a.params[1], a.params[2], a.params[3]};
        if (a.kind == MaterialKind::Custom && !a.customShaderPath.empty())
            m.customShaderPath = toAbs(lib, a.customShaderPath);
    }
}
} // namespace

Material resolveMaterial(const ChartData::MaterialData& md,
                         const MaterialAssetLibrary* lib) {
    Material m;
    if (!md.assetName.empty() && lib) {
        if (const MaterialAsset* a = lib->get(md.assetName)) {
            fillFromAsset(m, *a, lib);
            return m;
        }
        // Asset name referenced but not found — fall through to inline so the
        // visual doesn't silently disappear.
    }
    // Legacy inline (no assetName): synthesize an asset and run the same
    // v1→v2 mapping so inline unlit/glow still becomes lit PBR.
    MaterialAsset a;
    a.kind   = parseKind(md.kind);
    a.tint   = {md.tint[0],   md.tint[1],   md.tint[2],   md.tint[3]};
    a.params = {md.params[0], md.params[1], md.params[2], md.params[3]};
    a.texturePath = md.texturePath;
    migrateAssetV1toV2(a);
    fillFromAsset(m, a, lib);
    return m;
}
