#include "GameplayHudView.h"
#include "engine/IPlayerEngine.h"
#include "gameplay/ScoreTracker.h"
#include "ui/ProjectHub.h"  // full GameModeConfig + HudTextConfig
#include <imgui.h>
#include <cstdio>
#include <cfloat>

void GameplayHudView::render(ImVec2 displaySize, IPlayerEngine& engine) {
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float sw = displaySize.x, sh = displaySize.y;
    const float ui = m_uiScale;

    const GameModeConfig& cfg = engine.gameplayConfig();
    ScoreTracker& score = engine.score();

    auto drawHud = [&](const HudTextConfig& h, const char* text) {
        if (!text || text[0] == '\0') return;
        float fx = sw * h.pos[0];
        float fy = sh * h.pos[1];
        float fs = h.fontSize * h.scale * ui;
        ImU32 col = IM_COL32((int)(h.color[0]*255), (int)(h.color[1]*255),
                             (int)(h.color[2]*255), (int)(h.color[3]*255));
        ImFont* font = ImGui::GetFont();
        ImVec2 textSz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, text);
        ImVec2 textPos(fx - textSz.x * 0.5f, fy - textSz.y * 0.5f);

        // h.glow / h.bold are intentionally ignored on the in-game HUD.
        // Both rely on offset double-draws (8-direction halo, +1px shadow)
        // that on a high-DPI phone read as bloom/smear and made the combo
        // number unreadable. Real bold needs a bold font face; real glow
        // needs the post-process bloom path. Single crisp pass only.
        (void)h.glow; (void)h.bold;
        dl->AddText(font, fs, textPos, col, text);
    };

    // Score panel
    {
        char scoreBuf[32];
        snprintf(scoreBuf, sizeof(scoreBuf), "%d", score.getScore());

        const HudTextConfig& sh_ = cfg.scoreHud;
        float fs = sh_.fontSize * sh_.scale * ui;
        ImFont* font = ImGui::GetFont();
        ImVec2 textSz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, scoreBuf);
        float fx = sw * sh_.pos[0], fy = sh * sh_.pos[1];
        float pad = 8.f * ui;
        dl->AddRectFilled(
            ImVec2(fx - textSz.x / 2 - pad, fy - textSz.y / 2 - pad / 2),
            ImVec2(fx + textSz.x / 2 + pad, fy + textSz.y / 2 + pad / 2),
            IM_COL32(0, 0, 0, 140), 6.f);

        drawHud(cfg.scoreHud, scoreBuf);

        HudTextConfig scoreLabel = cfg.scoreHud;
        scoreLabel.pos[1] -= scoreLabel.fontSize * scoreLabel.scale / sh * 1.1f;
        scoreLabel.fontSize *= 0.4f;
        scoreLabel.glow = false;
        drawHud(scoreLabel, "SCORE");
    }

    // Combo panel
    {
        int combo = score.getCombo();
        char comboBuf[32];
        snprintf(comboBuf, sizeof(comboBuf), "%d", combo);

        const HudTextConfig& ch = cfg.comboHud;
        float fs = ch.fontSize * ch.scale * ui;
        ImFont* font = ImGui::GetFont();
        ImVec2 textSz = font->CalcTextSizeA(fs, FLT_MAX, 0.f, comboBuf);
        float fx = sw * ch.pos[0], fy = sh * ch.pos[1];
        float pad = 10.f * ui;
        float panelH = textSz.y + ch.fontSize * ch.scale * ui * 0.5f + pad * 2;
        dl->AddRectFilled(
            ImVec2(fx - textSz.x / 2 - pad * 2, fy - textSz.y / 2 - pad),
            ImVec2(fx + textSz.x / 2 + pad * 2, fy + panelH - pad),
            IM_COL32(0, 0, 0, 120), 8.f);

        drawHud(cfg.comboHud, comboBuf);

        HudTextConfig comboLabel = cfg.comboHud;
        comboLabel.pos[1] += comboLabel.fontSize * comboLabel.scale / sh * 1.2f;
        comboLabel.fontSize *= 0.45f;
        drawHud(comboLabel, "COMBO");
    }

    // Top-left Stop button — phones have no Esc key. Hosted in a tiny
    // borderless window so InvisibleButton has an ImGui window to attach to.
    {
        float btnSize = 44.f * ui;
        float margin  = 16.f * ui;
        ImVec2 tl(margin, margin);
        ImVec2 br(margin + btnSize, margin + btnSize);

        ImGui::SetNextWindowPos(tl);
        ImGui::SetNextWindowSize(ImVec2(btnSize, btnSize));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::Begin("##stop_window", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        bool clicked = ImGui::InvisibleButton("##stop_btn", ImVec2(btnSize, btnSize));
        bool hovered = ImGui::IsItemHovered();

        ImU32 bg     = hovered ? IM_COL32(220, 60, 60, 220) : IM_COL32(0, 0, 0, 160);
        ImU32 border = IM_COL32(255, 255, 255, hovered ? 220 : 160);
        ImU32 icon   = IM_COL32(255, 255, 255, 230);

        dl->AddRectFilled(tl, br, bg, 8.f * ui);
        dl->AddRect(tl, br, border, 8.f * ui, 0, 1.5f * ui);

        // Filled square = "stop" glyph
        float pad = btnSize * 0.28f;
        dl->AddRectFilled(ImVec2(tl.x + pad, tl.y + pad),
                          ImVec2(br.x - pad, br.y - pad),
                          icon, 2.f * ui);

        ImGui::End();
        ImGui::PopStyleVar(2);

        if (clicked) engine.requestStop();
    }
}
