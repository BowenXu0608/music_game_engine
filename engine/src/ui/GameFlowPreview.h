#pragma once
#include "StartScreenEditor.h"
#include <imgui.h>

class Engine;

enum class FlowPage { StartScreen, MusicSelection };

class GameFlowPreview {
public:
    void render(ImVec2 origin, ImVec2 size, Engine* engine);
    void reset();

private:
    void renderControls(ImVec2 origin, float width, float controlH, Engine* engine);
    void renderPage(FlowPage page, ImVec2 origin, ImVec2 size, Engine* engine);
    void renderTransition(ImVec2 origin, ImVec2 size, Engine* engine);
    void startTransition(FlowPage target, Engine* engine);

    FlowPage         m_currentPage   = FlowPage::StartScreen;
    FlowPage         m_targetPage    = FlowPage::StartScreen;
    bool             m_playing       = false;
    bool             m_transitioning = false;
    float            m_transitionProgress = 0.f;
    float            m_transitionDur      = 0.5f;
    TransitionEffect m_activeEffect  = TransitionEffect::Fade;
};
