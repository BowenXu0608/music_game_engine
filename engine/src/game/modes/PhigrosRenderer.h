#pragma once
#include "GameModeRenderer.h"
#include "core/SceneNode.h"
#include <vector>
#include <unordered_map>

class PhigrosRenderer : public GameModeRenderer {
public:
    void onInit(Renderer& renderer, const ChartData& chart) override;
    void onResize(uint32_t w, uint32_t h) override;
    void onUpdate(float dt, double songTime) override;
    void onRender(Renderer& renderer) override;
    void onShutdown(Renderer& renderer) override;
    const Camera& getCamera() const override { return m_camera; }

    // Returns screen-space origin and rotation of each active judgment line
    struct LineInfo { glm::vec2 origin; float rotation; };
    std::vector<LineInfo> getActiveLines() const;

private:
    struct LineState {
        NodeID    nodeID;
        glm::vec2 position;
        float     rotation;
        float     speed;
        std::vector<NoteEvent> notes;
    };

    void updateLineKeyframes(double songTime);

    Camera     m_camera;
    SceneGraph m_scene;
    uint32_t   m_width = 0, m_height = 0;
    double     m_songTime = 0.0;

    std::vector<LineState>              m_lines;
    std::vector<JudgmentLineEvent>      m_lineEvents;
    std::unordered_map<uint32_t, NodeID> m_noteNodes;  // noteID → scene node
};
