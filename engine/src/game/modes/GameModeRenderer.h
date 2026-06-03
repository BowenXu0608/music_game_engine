#pragma once
#include "renderer/Camera.h"
#include "game/chart/ChartTypes.h"
#include "gameplay/JudgmentSystem.h"
#include "gameplay/JudgmentDisplay.h"
#include <unordered_set>
#include <vector>
#include <cstdint>

class Renderer;
struct GameModeConfig;
class  MaterialAssetLibrary;
class  ParticleEffectLibrary;

// Gameplay moments that can trigger a particle effect. Each maps to a particle
// event-slot slug (see renderer/ParticleSlots.h).
enum class HitEventKind {
    ClickHit,   // click_hit
    FlickHit,   // flick_hit
    HoldHead,   // hold_head
    HoldTick,   // hold_tick
    HoldEnd     // hold_end
    // (slide ticks are ScanLine-only; wired in Phase 5)
};

class GameModeRenderer {
public:
    virtual ~GameModeRenderer() = default;

    virtual void onInit(Renderer& renderer, const ChartData& chart,
                        const GameModeConfig* config = nullptr) = 0;
    virtual void onResize(uint32_t width, uint32_t height) = 0;
    virtual void onUpdate(float dt, double songTime) = 0;
    virtual void onRender(Renderer& renderer) = 0;
    virtual void onShutdown(Renderer& renderer) = 0;
    virtual const Camera& getCamera() const = 0;

    // `timingDelta` is the hit's timing error in seconds (note.time - hitTime):
    // > 0 = early, < 0 = late, 0 = none (sample ticks / misses). Drives the
    // floating judgment text's early/late label.
    virtual void showJudgment(int lane, Judgment judgment, float timingDelta = 0.f) {}

    // Active floating judgment-text entries for the HUD to draw. Default empty
    // (modes without a judgment-text overlay). Bandori populates this.
    virtual const std::vector<JudgmentDisplay>& judgmentDisplays() const {
        static const std::vector<JudgmentDisplay> kEmpty;
        return kEmpty;
    }

    // Emit the particle effect bound to a gameplay event. Distinct from
    // showJudgment (which drives the judgment TEXT). Engine calls this from the
    // hit-detection paths; renderers resolve the bound ParticleEffectAsset and
    // emit at the event's on-screen/world position. Default: no-op.
    virtual void showHitEffect(HitEventKind kind, int lane,
                               NoteType type, Judgment judgment) {}

    // Called once per frame so the renderer can emit the sustained "hold aura"
    // for every note in m_activeHoldIds. Default: no-op.
    virtual void emitHoldAura(float dt) {}

    // Set by Engine each frame so renderers can highlight hold notes that
    // are currently being held (bloom/glow visual feedback).
    void setActiveHoldIds(const std::vector<uint32_t>& ids) {
        m_activeHoldIds.clear();
        for (uint32_t id : ids) m_activeHoldIds.insert(id);
    }

    // Engine sets this to `true` for the instance used as the editor preview,
    // and leaves it `false` (default) for the gameplay instance. Renderers can
    // use this to show authoring-only overlays (lane guides, snap rulers, ...)
    // that must not appear during real play.
    void setEditorPreview(bool v) { m_isEditorPreview = v; }
    bool isEditorPreview() const { return m_isEditorPreview; }

    // Engine injects the active project's MaterialAssetLibrary before onInit
    // so chart entries with an `assetName` can be resolved by name. Renderers
    // that don't receive a library (null) fall back to the inline legacy
    // fields on each MaterialData entry — keeps Android and standalone tests
    // working without the asset system.
    void setMaterialLibrary(MaterialAssetLibrary* lib) { m_materialLibrary = lib; }
    MaterialAssetLibrary* materialLibrary() const { return m_materialLibrary; }

    // Engine injects the project's ParticleEffectLibrary before onInit so
    // showHitEffect/emitHoldAura can resolve bound effects by name. Null = the
    // renderer skips particle emission (Android / standalone tests).
    void setParticleLibrary(ParticleEffectLibrary* lib) { m_particleLibrary = lib; }
    ParticleEffectLibrary* particleLibrary() const { return m_particleLibrary; }

    // Player-facing note-speed multiplier (1.0 = default). Drop modes and
    // Circle (Lanota) scale scroll speed / approach time by this value;
    // ScanLine (Cytus) and Phigros ignore it entirely.
    void setNoteSpeedMultiplier(float m) { m_noteSpeedMul = (m > 0.01f) ? m : 0.01f; }
    float noteSpeedMultiplier() const { return m_noteSpeedMul; }

protected:
    std::unordered_set<uint32_t> m_activeHoldIds;
    bool m_isEditorPreview = false;
    MaterialAssetLibrary* m_materialLibrary = nullptr;
    ParticleEffectLibrary* m_particleLibrary = nullptr;
    float m_noteSpeedMul = 1.f;
};
