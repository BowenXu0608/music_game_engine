#include "GameFlowPreview.h"
#include "engine/Engine.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

void GameFlowPreview::reset() {
    m_currentPage = FlowPage::StartScreen;
    m_targetPage  = FlowPage::StartScreen;
    m_playing     = false;
    m_transitioning = false;
    m_transitionProgress = 0.f;
}

void GameFlowPreview::startTransition(FlowPage target, Engine* engine) {
    if (m_transitioning || target == m_currentPage) return;
    m_targetPage  = target;
    m_transitioning = true;
    m_transitionProgress = 0.f;

    // Read effect settings from StartScreenEditor
    if (engine) {
        m_activeEffect = engine->startScreenEditor().transitionEffect();
        m_transitionDur = engine->startScreenEditor().transitionDuration();
    }
}

// ── Main render ──────────────────────────────────────────────────────────────

void GameFlowPreview::render(ImVec2 origin, ImVec2 size, Engine* engine) {
    float controlH = 32.f;

    renderControls(origin, size.x, controlH, engine);

    ImVec2 previewOrigin(origin.x, origin.y + controlH + 4.f);
    ImVec2 previewSize(size.x, size.y - controlH - 4.f);

    if (previewSize.y <= 0.f) return;

    // Update transition
    if (m_transitioning && m_playing) {
        float dt = ImGui::GetIO().DeltaTime;
        m_transitionProgress += dt / std::max(0.1f, m_transitionDur);
        if (m_transitionProgress >= 1.f) {
            m_transitionProgress = 0.f;
            m_transitioning = false;
            m_currentPage = m_targetPage;
        }
    }

    // Render
    if (m_transitioning && m_playing) {
        renderTransition(previewOrigin, previewSize, engine);
    } else {
        renderPage(m_currentPage, previewOrigin, previewSize, engine);
    }

    // Click-to-advance
    if (m_playing && !m_transitioning && m_currentPage == FlowPage::StartScreen) {
        ImVec2 mousePos = ImGui::GetIO().MousePos;
        if (mousePos.x >= previewOrigin.x && mousePos.x <= previewOrigin.x + previewSize.x &&
            mousePos.y >= previewOrigin.y && mousePos.y <= previewOrigin.y + previewSize.y) {
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                startTransition(FlowPage::MusicSelection, engine);
            }
        }
    }
}

// ── Controls ─────────────────────────────────────────────────────────────────

void GameFlowPreview::renderControls(ImVec2 origin, float width, float controlH, Engine* engine) {
    ImGui::SetCursorScreenPos(origin);

    if (m_playing) {
        if (ImGui::Button("Stop")) {
            m_playing = false;
            m_transitioning = false;
        }
    } else {
        if (ImGui::Button("Play")) {
            m_playing = true;
        }
    }

    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        reset();
        m_playing = true;
    }

    ImGui::SameLine();
    // Show which effect is active (read from StartScreenEditor)
    const char* effectNames[] = { "Fade", "Slide Left", "Zoom In", "Ripple", "Custom" };
    const char* effectName = effectNames[(int)m_activeEffect];
    ImGui::TextDisabled("Effect: %s (%.1fs)", effectName, m_transitionDur);

    ImGui::SameLine();
    const char* pageName = (m_currentPage == FlowPage::StartScreen) ? "Start Screen" : "Music Selection";
    ImGui::TextDisabled("| Page: %s", pageName);

    if (m_transitioning) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.2f, 1.f), "%.0f%%", m_transitionProgress * 100.f);
    }
}

// ── Page rendering ───────────────────────────────────────────────────────────

void GameFlowPreview::renderPage(FlowPage page, ImVec2 origin, ImVec2 size, Engine* engine) {
    if (!engine) return;

    switch (page) {
        case FlowPage::StartScreen:
            engine->startScreenEditor().renderGamePreview(origin, size);
            break;
        case FlowPage::MusicSelection:
            engine->musicSelectionEditor().renderGamePreview(origin, size);
            break;
    }
}

// ── Transition rendering ─────────────────────────────────────────────────────

void GameFlowPreview::renderTransition(ImVec2 origin, ImVec2 size, Engine* engine) {
    if (!engine) return;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = m_transitionProgress;
    float eased = t * t * (3.f - 2.f * t); // smoothstep

    dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);

    switch (m_activeEffect) {
        case TransitionEffect::Fade: {
            // Cross-fade: render old page, then overlay new page with increasing alpha
            renderPage(m_currentPage, origin, size, engine);

            // Dark overlay fading out as new page fades in
            int oldAlpha = (int)(255 * eased);
            dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                              IM_COL32(0, 0, 0, oldAlpha));

            // New page rendered on top (simulated alpha via darkening old + brightening new)
            // Since ImDrawList doesn't support true layer alpha, we render the new page
            // and darken it based on remaining transition
            renderPage(m_targetPage, origin, size, engine);
            int newDarken = (int)(255 * (1.f - eased));
            dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                              IM_COL32(0, 0, 0, newDarken));
            break;
        }

        case TransitionEffect::SlideLeft: {
            // Old page slides out left, new page slides in from right
            float offsetOld = -eased * size.x;
            float offsetNew = (1.f - eased) * size.x;

            renderPage(m_currentPage, ImVec2(origin.x + offsetOld, origin.y), size, engine);
            renderPage(m_targetPage,  ImVec2(origin.x + offsetNew, origin.y), size, engine);

            // Subtle darkening at midpoint
            int fadeAlpha = (int)(30.f * (1.f - std::abs(eased - 0.5f) * 2.f));
            if (fadeAlpha > 0)
                dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                                  IM_COL32(0, 0, 0, fadeAlpha));
            break;
        }

        case TransitionEffect::ZoomIn: {
            // Old page zooms in and fades out, new page appears underneath
            renderPage(m_targetPage, origin, size, engine);

            // Darken new page early in transition
            int newDarken = (int)(200 * (1.f - eased));
            if (newDarken > 0)
                dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                                  IM_COL32(0, 0, 0, newDarken));

            // Old page zooms in (scale 1.0 → 1.5) and fades out
            float scale = 1.f + eased * 0.5f;
            float zoomW = size.x * scale;
            float zoomH = size.y * scale;
            float cx = origin.x + size.x * 0.5f;
            float cy = origin.y + size.y * 0.5f;
            ImVec2 zoomOrigin(cx - zoomW * 0.5f, cy - zoomH * 0.5f);
            ImVec2 zoomSize(zoomW, zoomH);

            renderPage(m_currentPage, zoomOrigin, zoomSize, engine);

            // Fade out old page
            int oldFade = (int)(255 * eased);
            dl->AddRectFilled(ImVec2(cx - zoomW * 0.5f, cy - zoomH * 0.5f),
                              ImVec2(cx + zoomW * 0.5f, cy + zoomH * 0.5f),
                              IM_COL32(0, 0, 0, oldFade));
            break;
        }

        case TransitionEffect::Ripple: {
            // Circular reveal: new page revealed in an expanding circle from center
            renderPage(m_currentPage, origin, size, engine);

            // Calculate circle radius
            float maxRadius = std::sqrt(size.x * size.x + size.y * size.y) * 0.5f;
            float radius = eased * maxRadius;
            float cx = origin.x + size.x * 0.5f;
            float cy = origin.y + size.y * 0.5f;

            // We can't do true circular masking with ImDrawList, so simulate with
            // rendering new page fully, then covering the outside with the old page's color
            // Use a dark circle expanding effect instead:

            // Render new page
            renderPage(m_targetPage, origin, size, engine);

            // Draw dark overlay with a "hole" in the center using 4 rects around the circle
            // Approximate: darken everything, then brighten the circle area
            int coverAlpha = (int)(220 * (1.f - eased));
            dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                              IM_COL32(0, 0, 0, coverAlpha));

            // Bright circle in center (new page showing through)
            int circleSegments = 32;
            float circleAlpha = (float)coverAlpha / 255.f;
            // Draw the revealing circle by undoing the darkness
            // Since we can't subtract, draw the new page content clipped to circle
            // ... simplified: just draw a filled circle that "erases" the dark overlay
            dl->AddCircleFilled(ImVec2(cx, cy), radius,
                                IM_COL32(0, 0, 0, 0), circleSegments);
            // The circle won't truly erase. Instead use a ring of brightness:
            // Simpler approach: render a glow ring at the edge
            dl->AddCircle(ImVec2(cx, cy), radius,
                          IM_COL32(255, 255, 255, (int)(80 * (1.f - eased))),
                          circleSegments, 3.f);
            break;
        }

        case TransitionEffect::Custom:
        default: {
            // Fallback: same as Fade
            renderPage(m_currentPage, origin, size, engine);
            int oldAlpha = (int)(255 * eased);
            dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                              IM_COL32(0, 0, 0, oldAlpha));
            renderPage(m_targetPage, origin, size, engine);
            int newDarken = (int)(255 * (1.f - eased));
            dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                              IM_COL32(0, 0, 0, newDarken));
            break;
        }
    }

    dl->PopClipRect();
}
