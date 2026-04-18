#pragma once
#include "renderer/Camera.h"
#include "game/chart/ChartTypes.h"
#include "gameplay/JudgmentSystem.h"
#include <unordered_set>
#include <cstdint>

class Renderer;
struct GameModeConfig;
class  MaterialAssetLibrary;

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

    virtual void showJudgment(int lane, Judgment judgment) {}

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

protected:
    std::unordered_set<uint32_t> m_activeHoldIds;
    bool m_isEditorPreview = false;
    MaterialAssetLibrary* m_materialLibrary = nullptr;
};
