#include "ParticleEffectLibrary.h"
#include "ParticleSlots.h"
#include <algorithm>
#include <system_error>

namespace fs = std::filesystem;

const std::vector<ParticleSlotInfo>& bandoriParticleSlots() {
    static const std::vector<ParticleSlotInfo> slots = {
        {"click_hit", "Click Hit"},
        {"flick_hit", "Flick Hit"},
        {"hold_head", "Hold Head"},
        {"hold_tick", "Hold Tick"},
        {"hold_aura", "Hold Aura"},
        {"hold_end",  "Hold End"},
    };
    return slots;
}

const std::vector<ParticleSlotInfo>& cytusParticleSlots() {
    static const std::vector<ParticleSlotInfo> slots = {
        {"click_hit",  "Click Hit"},
        {"flick_hit",  "Flick Hit"},
        {"hold_head",  "Hold Head"},
        {"hold_end",   "Hold End"},
        {"slide_tick", "Slide Sample"},
    };
    return slots;
}

const std::vector<ParticleSlotInfo>& arcaeaParticleSlots() {
    static const std::vector<ParticleSlotInfo> slots = {
        {"click_hit", "Ground Tap"},
        {"flick_hit", "Flick"},
        {"hold_head", "Hold Head"},
        {"hold_tick", "Hold Tick"},
        {"hold_end",  "Hold End"},
        {"arc",       "Arc"},
        {"arctap",    "Arc-Tap (Sky)"},
    };
    return slots;
}

const std::vector<ParticleSlotInfo>& lanotaParticleSlots() {
    static const std::vector<ParticleSlotInfo> slots = {
        {"click_hit", "Note Hit"},
        {"flick_hit", "Flick Hit"},
        {"hold_tick", "Hold Tick"},
    };
    return slots;
}

const std::vector<ParticleSlotInfo>& particleSlotsForMode(const std::string& mode) {
    if (mode == "cytus")  return cytusParticleSlots();
    if (mode == "arcaea") return arcaeaParticleSlots();
    if (mode == "lanota") return lanotaParticleSlots();
    return bandoriParticleSlots();
}

fs::path ParticleEffectLibrary::particlesDir() const {
    return m_projectDir / "assets" / "particles";
}

void ParticleEffectLibrary::loadFromProject(const fs::path& projectDir) {
    m_projectDir = projectDir;
    m_assets.clear();

    fs::path dir = particlesDir();
    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return;

    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".pfx") continue;
        ParticleEffectAsset asset;
        if (!loadParticleEffectAsset(entry.path(), asset)) continue;
        if (asset.name.empty()) continue;
        m_assets[asset.name] = asset;
    }
}

void ParticleEffectLibrary::clear() {
    m_assets.clear();
    m_projectDir.clear();
}

const ParticleEffectAsset* ParticleEffectLibrary::get(const std::string& name) const {
    auto it = m_assets.find(name);
    return it == m_assets.end() ? nullptr : &it->second;
}

bool ParticleEffectLibrary::upsert(const ParticleEffectAsset& asset) {
    if (asset.name.empty()) return false;
    std::error_code ec;
    fs::create_directories(particlesDir(), ec);
    fs::path p = particlesDir() / (asset.name + ".pfx");
    if (!saveParticleEffectAsset(asset, p)) return false;
    m_assets[asset.name] = asset;
    return true;
}

void ParticleEffectLibrary::remove(const std::string& name) {
    auto it = m_assets.find(name);
    if (it == m_assets.end()) return;
    m_assets.erase(it);
    std::error_code ec;
    fs::remove(particlesDir() / (name + ".pfx"), ec);
}

std::vector<std::string> ParticleEffectLibrary::allNames() const {
    std::vector<std::string> out;
    out.reserve(m_assets.size());
    for (auto& [name, _] : m_assets) out.push_back(name);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> ParticleEffectLibrary::namesCompatibleWith(
    const std::string& mode, const std::string& slug) const {
    std::vector<std::string> out;
    for (auto& [name, a] : m_assets) {
        if (!a.targetMode.empty()     && a.targetMode     != mode) continue;
        if (!a.targetSlotSlug.empty() && a.targetSlotSlug != slug) continue;
        out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

namespace {
// Built-in default for one Bandori event slot. Distinct hue + pattern per slot
// so every gameplay moment reads differently out of the box.
ParticleEffectAsset defaultForSlot(const std::string& mode, const std::string& slug) {
    ParticleEffectAsset a;
    a.name           = defaultParticleEffectName(mode, slug);
    a.targetMode     = mode;
    a.targetSlotSlug = slug;

    if (slug == "click_hit") {
        a.kind = ParticleEffectKind::Burst;
        a.count = 18; a.speedMin = 140.f; a.speedMax = 280.f;
        a.sizeStart = 9.f; a.sizeEnd = 2.f; a.lifeMin = 0.30f; a.lifeMax = 0.5f;
        a.color = {0.35f, 1.0f, 0.6f, 1.f}; a.colorEnd = {0.35f, 1.0f, 0.6f, 0.f};
    } else if (slug == "flick_hit") {
        a.kind = ParticleEffectKind::Spark;
        a.count = 22; a.speedMin = 220.f; a.speedMax = 420.f; a.spread = 1.4f;
        a.sizeStart = 8.f; a.sizeEnd = 1.5f; a.lifeMin = 0.30f; a.lifeMax = 0.55f;
        a.gravity = {0.f, 520.f};   // screen space: +y falls
        a.color = {0.45f, 0.8f, 1.0f, 1.f}; a.colorEnd = {0.45f, 0.8f, 1.0f, 0.f};
    } else if (slug == "hold_head") {
        a.kind = ParticleEffectKind::Ring;
        a.count = 26; a.speedMin = 200.f; a.speedMax = 200.f;
        a.sizeStart = 7.f; a.sizeEnd = 2.f; a.lifeMin = 0.32f; a.lifeMax = 0.42f;
        a.color = {0.7f, 1.0f, 1.0f, 1.f}; a.colorEnd = {0.7f, 1.0f, 1.0f, 0.f};
    } else if (slug == "hold_tick") {
        a.kind = ParticleEffectKind::Burst;
        a.count = 8; a.speedMin = 80.f; a.speedMax = 180.f;
        a.sizeStart = 6.f; a.sizeEnd = 1.5f; a.lifeMin = 0.20f; a.lifeMax = 0.35f;
        a.color = {0.45f, 0.95f, 1.0f, 1.f}; a.colorEnd = {0.45f, 0.95f, 1.0f, 0.f};
    } else if (slug == "hold_aura") {
        a.kind = ParticleEffectKind::Aura;
        a.rateHz = 110.f; a.speedMin = 30.f; a.speedMax = 90.f; a.spread = 1.6f;
        a.sizeStart = 6.f; a.sizeEnd = 1.f; a.lifeMin = 0.30f; a.lifeMax = 0.55f;
        a.color = {0.55f, 0.95f, 1.0f, 1.f}; a.colorEnd = {0.55f, 0.95f, 1.0f, 0.f};
    } else if (slug == "hold_end") {
        a.kind = ParticleEffectKind::Spark;
        a.count = 24; a.speedMin = 180.f; a.speedMax = 360.f; a.spread = 6.2831853f;
        a.sizeStart = 9.f; a.sizeEnd = 2.f; a.lifeMin = 0.35f; a.lifeMax = 0.6f;
        a.color = {1.0f, 0.9f, 0.5f, 1.f}; a.colorEnd = {1.0f, 0.9f, 0.5f, 0.f};
    } else if (slug == "slide_tick") {
        a.kind = ParticleEffectKind::Burst;
        a.count = 12; a.speedMin = 90.f; a.speedMax = 200.f;
        a.sizeStart = 7.f; a.sizeEnd = 1.5f; a.lifeMin = 0.22f; a.lifeMax = 0.4f;
        a.color = {0.5f, 1.0f, 0.7f, 1.f}; a.colorEnd = {0.5f, 1.0f, 0.7f, 0.f};
    } else if (slug == "sky_hit") {
        a.kind = ParticleEffectKind::Burst;
        a.count = 18; a.speedMin = 140.f; a.speedMax = 280.f;
        a.sizeStart = 9.f; a.sizeEnd = 2.f; a.lifeMin = 0.30f; a.lifeMax = 0.5f;
        a.color = {0.5f, 0.7f, 1.0f, 1.f}; a.colorEnd = {0.5f, 0.7f, 1.0f, 0.f};
    } else if (slug == "arc") {
        // Arc trace hit — Arcaea arcs are blue/pink; default to a soft cyan ring.
        a.kind = ParticleEffectKind::Ring;
        a.count = 20; a.speedMin = 180.f; a.speedMax = 180.f;
        a.sizeStart = 8.f; a.sizeEnd = 2.f; a.lifeMin = 0.30f; a.lifeMax = 0.45f;
        a.color = {0.4f, 0.85f, 1.0f, 1.f}; a.colorEnd = {0.4f, 0.85f, 1.0f, 0.f};
    } else if (slug == "arctap") {
        // Sky short note sitting on an arc — bright magenta spark.
        a.kind = ParticleEffectKind::Burst;
        a.count = 18; a.speedMin = 140.f; a.speedMax = 300.f;
        a.sizeStart = 8.f; a.sizeEnd = 1.8f; a.lifeMin = 0.28f; a.lifeMax = 0.46f;
        a.color = {1.0f, 0.55f, 0.95f, 1.f}; a.colorEnd = {1.0f, 0.55f, 0.95f, 0.f};
    }

    // Arcaea emits particles in WORLD space (its camera is 3D perspective),
    // not screen pixels — rescale the screen-tuned defaults to world units.
    if (mode == "arcaea") {
        constexpr float kWorld = 0.012f;   // ~200px -> 2.4 world units
        a.speedMin  *= kWorld; a.speedMax *= kWorld;
        a.sizeStart *= 0.02f;  a.sizeEnd  *= 0.02f;
        a.gravity[0] *= kWorld; a.gravity[1] *= kWorld;
    }
    return a;
}
} // namespace

void ParticleEffectLibrary::seedDefaultEffects(const std::string& mode) {
    const auto& slots = particleSlotsForMode(mode);
    for (const auto& slot : slots) {
        std::string name = defaultParticleEffectName(mode, slot.slug);
        if (m_assets.find(name) != m_assets.end()) continue;   // preserve edits
        upsert(defaultForSlot(mode, slot.slug));
    }
}

void ParticleEffectLibrary::seedUiTapEffect() {
    if (m_assets.find(kUiTapEffectName) != m_assets.end()) return;  // preserve edits
    ParticleEffectAsset a;
    a.name      = kUiTapEffectName;
    a.kind      = ParticleEffectKind::Burst;
    a.count     = 12;
    a.speedMin  = 90.f;   a.speedMax = 180.f;
    a.sizeStart = 8.f;    a.sizeEnd  = 1.f;
    a.lifeMin   = 0.22f;  a.lifeMax  = 0.4f;
    a.color     = {1.0f, 1.0f, 1.0f, 0.95f};
    a.colorEnd  = {0.8f, 0.9f, 1.0f, 0.f};
    // Mode-independent: leave targetMode/targetSlotSlug empty.
    upsert(a);
}
