#pragma once
#include "GameModeRenderer.h"
#include "renderer/vulkan/DescriptorManager.h"
#include <vector>
#include <unordered_set>

class BandoriRenderer : public GameModeRenderer {
public:
    void onInit(Renderer& renderer, const ChartData& chart) override;
    void onResize(uint32_t w, uint32_t h) override;
    void onUpdate(float dt, double songTime) override;
    void onRender(Renderer& renderer) override;
    void onShutdown(Renderer& renderer) override;
    const Camera& getCamera() const override { return m_camera; }

private:
    Camera   m_camera;
    float    m_scrollSpeed = 600.f;
    float    m_hitZoneY    = 0.f;
    float    m_laneWidth   = 0.f;
    float    m_laneStartX  = 0.f;
    uint32_t m_width = 0, m_height = 0;
    double   m_songTime = 0.0;

    std::vector<NoteEvent>      m_notes;
    std::unordered_set<uint32_t> m_hitNotes;  // IDs of notes that already fired hit effect
};
