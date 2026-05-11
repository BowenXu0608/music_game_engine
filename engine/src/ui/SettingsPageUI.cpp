#include "SettingsPageUI.h"
#include "StyleTokens.h"
#include "Widgets.h"

#include "engine/AudioEngine.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cmath>

namespace {

struct CalibrationState {
    bool   active = false;
    bool   finished = false;
    double startTime = 0.0;
    double beatPeriod = 1.0;
    int    beatsTotal = 4;
    int    beatsTicked = 0;
    std::vector<double> tapDeltas;
    double resultMs = 0.0;
};

static CalibrationState& calState() {
    static CalibrationState s;
    return s;
}

static const char* kLanguageChoices[] = { "en", "zh", "ja", "ko" };

void drawGearIcon(ImDrawList* dl, ImVec2 center, float r, ImU32 col) {
    dl->AddCircle(center, r * 0.5f, col, 0, 1.5f);
    for (int i = 0; i < 6; ++i) {
        float a = (float)i * (3.14159f * 2.f / 6.f);
        ImVec2 outer = {center.x + cosf(a) * r, center.y + sinf(a) * r};
        ImVec2 inner = {center.x + cosf(a) * r * 0.65f, center.y + sinf(a) * r * 0.65f};
        dl->AddLine(inner, outer, col, 2.f);
    }
}

void drawCloseX(ImDrawList* dl, ImVec2 center, float sz, ImU32 col) {
    dl->AddLine({center.x - sz, center.y - sz}, {center.x + sz, center.y + sz}, col, 1.5f);
    dl->AddLine({center.x + sz, center.y - sz}, {center.x - sz, center.y + sz}, col, 1.5f);
}

void drawCalibrationPanel(PlayerSettings& s, AudioEngine* audio, bool readOnly) {
    using namespace ui::tokens;
    auto& cs = calState();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (!cs.active && !cs.finished) {
        const ImVec2 cardOrigin = ImGui::GetCursorScreenPos();
        const float cardW = ImGui::GetContentRegionAvail().x;
        const float cardH = 100.f;
        const ImVec2 cardMax = {cardOrigin.x + cardW, cardOrigin.y + cardH};

        dl->AddRectFilled(cardOrigin, cardMax, ToU32(BgPanel2), 8.f);
        dl->AddRect(cardOrigin, cardMax, ToU32(WithAlpha(Cyan, 0.3f)), 8.f);

        ImGui::Dummy({cardW, 12.f});
        ImGui::Indent(16.f);
        ImGui::PushStyleColor(ImGuiCol_Text, TextMid);
        ImGui::TextWrapped("Tap along with a metronome to measure your device latency.");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        const bool canCalibrate = (audio != nullptr) && !readOnly;
        if (!canCalibrate) ImGui::BeginDisabled();
        if (ui::GhostButton("Start Calibration", {-17.f, 32.f}) && canCalibrate) {
            cs.active = true;
            cs.finished = false;
            cs.startTime = ImGui::GetTime() + 1.0;
            cs.beatsTicked = 0;
            cs.tapDeltas.clear();
        }
        if (!canCalibrate) ImGui::EndDisabled();
        ImGui::Unindent(16.f);
        ImGui::Dummy({cardW, 4.f});
        ImGui::SetCursorScreenPos({cardOrigin.x, cardMax.y + 4.f});
        return;
    }

    if (cs.active) {
        const double now = ImGui::GetTime();
        const double tSinceStart = now - cs.startTime;

        while (cs.beatsTicked < cs.beatsTotal &&
               tSinceStart >= cs.beatsTicked * cs.beatPeriod) {
            if (audio) audio->playClickSfx();
            cs.beatsTicked++;
        }

        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const double beatIdx = std::round(tSinceStart / cs.beatPeriod);
            const double expectedT = beatIdx * cs.beatPeriod;
            const double dt = tSinceStart - expectedT;
            if (std::abs(dt) < cs.beatPeriod * 0.5 && beatIdx >= 0 && beatIdx < cs.beatsTotal)
                cs.tapDeltas.push_back(dt);
        }

        const ImVec2 cardOrigin = ImGui::GetCursorScreenPos();
        const float cardW = ImGui::GetContentRegionAvail().x;
        const float cardH = 80.f;
        const ImVec2 cardMax = {cardOrigin.x + cardW, cardOrigin.y + cardH};

        dl->AddRectFilled(cardOrigin, cardMax, ToU32(BgPanel2), 8.f);
        dl->AddRect(cardOrigin, cardMax, ToU32(WithAlpha(Magenta, 0.3f)), 8.f);

        // Beat dots
        const float dotY = cardOrigin.y + 24.f;
        const float dotSpacing = 28.f;
        const float dotsW = dotSpacing * (cs.beatsTotal - 1);
        float dotX = cardOrigin.x + (cardW - dotsW) * 0.5f;
        for (int i = 0; i < cs.beatsTotal; ++i) {
            bool ticked = (i < cs.beatsTicked);
            if (ticked)
                dl->AddCircleFilled({dotX, dotY}, 5.f, ToU32(Magenta));
            else
                dl->AddCircle({dotX, dotY}, 5.f, ToU32(WithAlpha(Magenta, 0.4f)), 0, 1.5f);
            dotX += dotSpacing;
        }

        // Tap zone pulsing
        float pulse = 0.5f + 0.3f * sinf((float)tSinceStart * 6.f);
        ImVec2 tapMin = {cardOrigin.x + 16.f, cardOrigin.y + 42.f};
        ImVec2 tapMax = {cardMax.x - 16.f, cardMax.y - 10.f};
        dl->AddRectFilled(tapMin, tapMax, ToU32(WithAlpha(Magenta, 0.06f * pulse)), 6.f);
        dl->AddRect(tapMin, tapMax, ToU32(WithAlpha(Magenta, 0.2f * pulse)), 6.f);

        ImFont* mono = ui::fonts.mono ? ui::fonts.mono : ImGui::GetFont();
        char progBuf[32];
        snprintf(progBuf, sizeof(progBuf), "Taps: %d", (int)cs.tapDeltas.size());
        ImVec2 progSz = mono->CalcTextSizeA(mono->FontSize, FLT_MAX, 0.f, progBuf);
        dl->AddText(mono, mono->FontSize,
            {cardOrigin.x + (cardW - progSz.x) * 0.5f, tapMin.y + 4.f},
            ToU32(TextMid), progBuf);

        ImGui::Dummy({cardW, cardH + 4.f});

        const bool done = (cs.beatsTicked >= cs.beatsTotal) &&
                          (tSinceStart > cs.beatsTotal * cs.beatPeriod + 0.5);
        if (done) {
            cs.active = false;
            cs.finished = true;
            cs.resultMs = 0.0;
            if (!cs.tapDeltas.empty()) {
                double sum = 0.0;
                for (double d : cs.tapDeltas) sum += d;
                cs.resultMs = (sum / (double)cs.tapDeltas.size()) * 1000.0;
            }
        }
        return;
    }

    // Finished state — show result
    const ImVec2 cardOrigin = ImGui::GetCursorScreenPos();
    const float cardW = ImGui::GetContentRegionAvail().x;
    const float cardH = 110.f;
    const ImVec2 cardMax = {cardOrigin.x + cardW, cardOrigin.y + cardH};

    dl->AddRectFilled(cardOrigin, cardMax, ToU32(BgPanel2), 8.f);
    dl->AddRect(cardOrigin, cardMax, ToU32(WithAlpha(Lime, 0.3f)), 8.f);

    // Large offset value
    ImFont* monoLg = ui::fonts.monoLg ? ui::fonts.monoLg : ImGui::GetFont();
    char resultBuf[32];
    snprintf(resultBuf, sizeof(resultBuf), "%+.1f ms", cs.resultMs);
    ImVec2 rSz = monoLg->CalcTextSizeA(monoLg->FontSize, FLT_MAX, 0.f, resultBuf);
    dl->AddText(monoLg, monoLg->FontSize,
        {cardOrigin.x + (cardW - rSz.x) * 0.5f, cardOrigin.y + 16.f},
        ToU32(Lime), resultBuf);

    ImGui::SetCursorScreenPos({cardOrigin.x + 16.f, cardOrigin.y + 52.f});
    ImGui::PushItemWidth(-17.f);

    const float btnW = (cardW - 48.f) / 3.f;
    if (ui::PrimaryButton("Accept", ui::tokens::Lime, {btnW, 32.f})) {
        s.audioOffsetMs = static_cast<float>(cs.resultMs);
        cs.finished = false;
    }
    ImGui::SameLine(0.f, 8.f);
    if (ui::GhostButton("Retry", {btnW, 32.f})) {
        cs.finished = false;
        cs.active = true;
        cs.startTime = ImGui::GetTime() + 1.0;
        cs.beatsTicked = 0;
        cs.tapDeltas.clear();
    }
    ImGui::SameLine(0.f, 8.f);
    if (ui::GhostButton("Cancel", {btnW, 32.f})) {
        cs.finished = false;
    }

    ImGui::PopItemWidth();
    ImGui::SetCursorScreenPos({cardOrigin.x, cardMax.y + 4.f});
}

} // namespace

void SettingsPageUI::render(ImVec2          origin,
                            ImVec2          size,
                            PlayerSettings& s,
                            const Host&     host,
                            bool            readOnly)
{
    using namespace ui::tokens;

    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, WithAlpha(BgVoid, 0.92f));
    ImGui::Begin("##settings_page_scrim", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    const float cardW = std::min(size.x - 80.f, 640.f);
    const float cardH = std::min(size.y - 60.f, 720.f);
    const float cardX = (size.x - cardW) * 0.5f;
    const float cardY = (size.y - cardH) * 0.5f;
    const float footerH = 56.f;

    ImGui::SetCursorPos(ImVec2(cardX, cardY));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 8));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, BgPanel);
    ImGui::PushStyleColor(ImGuiCol_Border,  BorderHi);
    ImGui::BeginChild("##settings_card", ImVec2(cardW, cardH), true,
        ImGuiWindowFlags_NoScrollbar);

    // ── Header ──────────────────────────────────────────────────────────
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 hOrigin = ImGui::GetCursorScreenPos();
        const float headerH = 44.f;

        // Gear icon
        drawGearIcon(dl, {hOrigin.x + 12.f, hOrigin.y + headerH * 0.5f}, 10.f, ToU32(TextMid));

        // Title
        ImFont* titleFont = ui::fonts.title ? ui::fonts.title : ImGui::GetFont();
        dl->AddText(titleFont, titleFont->FontSize,
            {hOrigin.x + 30.f, hOrigin.y + (headerH - titleFont->FontSize) * 0.5f},
            ToU32(TextHi), "Settings");

        // X close button
        const float closeBtnX = hOrigin.x + cardW - 24.f * 2.f - 12.f;
        ImGui::SetCursorScreenPos({closeBtnX, hOrigin.y + (headerH - 28.f) * 0.5f});
        if (ui::IconButton("##close", {28.f, 28.f}, drawCloseX)) {
            if (host.onBack) host.onBack();
        }

        // Divider below header
        ImGui::SetCursorScreenPos({hOrigin.x, hOrigin.y + headerH});
        dl->AddLine({hOrigin.x, hOrigin.y + headerH},
                    {hOrigin.x + cardW - 48.f, hOrigin.y + headerH},
                    ToU32(Divider), 1.f);
        ImGui::SetCursorScreenPos({hOrigin.x, hOrigin.y + headerH + 8.f});
    }

    if (readOnly) ImGui::BeginDisabled();

    // ── Scrollable body ─────────────────────────────────────────────────
    ImGui::BeginChild("##settings_body", ImVec2(0, -footerH), false);

    // ── Audio ───────────────────────────────────────────────────────────
    ui::SectionHeader("Audio");
    ImGui::Dummy({0, 4.f});

    ui::Slider("Music volume",     &s.musicVolume,    0.f, 1.f, "", Cyan);
    ImGui::Dummy({0, 6.f});
    ui::Slider("Hit-sound volume", &s.hitSoundVolume, 0.f, 1.f, "", Cyan);
    ImGui::Dummy({0, 6.f});
    ui::Toggle("Hit-Sound Enabled", &s.hitSoundEnabled, Cyan);

    if (!readOnly && host.audio) {
        host.audio->setMusicVolume(s.musicVolume);
        host.audio->setSfxVolume(s.hitSoundVolume);
        host.audio->setHitSoundEnabled(s.hitSoundEnabled);
    }

    ImGui::Dummy({0, 6.f});
    ui::Slider("Audio offset", &s.audioOffsetMs, -200.f, 200.f, " ms", Magenta);
    ImGui::Dummy({0, 8.f});
    drawCalibrationPanel(s, host.audio, readOnly);

    // ── Gameplay ────────────────────────────────────────────────────────
    ImGui::Dummy({0, 18.f});
    ui::SectionHeader("Gameplay");
    ImGui::Dummy({0, 4.f});
    ui::Slider("Note speed", &s.noteSpeed, 1.f, 10.f, "", Magenta);
    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
    ImGui::TextUnformatted("5 = default  |  Scan Line mode ignores this");
    ImGui::PopStyleColor();

    // ── Visual ──────────────────────────────────────────────────────────
    ImGui::Dummy({0, 18.f});
    ui::SectionHeader("Visual");
    ImGui::Dummy({0, 4.f});
    ui::Slider("Background dim", &s.backgroundDim, 0.f, 1.f, "", Lime);
    ImGui::Dummy({0, 6.f});
    ui::Toggle("Show FPS Counter", &s.fpsCounter, Lime);

    // ── Misc ────────────────────────────────────────────────────────────
    ImGui::Dummy({0, 18.f});
    ui::SectionHeader("Misc");
    ImGui::Dummy({0, 4.f});
    int langIdx = 0;
    for (int i = 0; i < (int)(sizeof(kLanguageChoices)/sizeof(kLanguageChoices[0])); ++i) {
        if (s.language == kLanguageChoices[i]) { langIdx = i; break; }
    }
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        BgPanel3);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, WithAlpha(Cyan, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg,        BgPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::Combo("##lang", &langIdx, kLanguageChoices,
                     (int)(sizeof(kLanguageChoices)/sizeof(kLanguageChoices[0])))) {
        s.language = kLanguageChoices[langIdx];
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
    ImGui::TextUnformatted("Stored only (localization not wired yet)");
    ImGui::PopStyleColor();

    ImGui::EndChild(); // settings_body

    if (readOnly) ImGui::EndDisabled();

    // ── Footer ──────────────────────────────────────────────────────────
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 fOrigin = ImGui::GetCursorScreenPos();
        dl->AddLine({fOrigin.x, fOrigin.y},
                    {fOrigin.x + cardW - 48.f, fOrigin.y},
                    ToU32(Divider), 1.f);

        ImGui::Dummy({0, 14.f});
        const float btnW = 90.f;
        const float btnGap = 8.f;
        const float totalBtnW = btnW * 2.f + btnGap;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - totalBtnW);

        if (ui::GhostButton("Cancel", {btnW, 32.f})) {
            if (host.onBack) host.onBack();
        }
        ImGui::SameLine(0.f, btnGap);
        if (readOnly) ImGui::BeginDisabled();
        if (ui::PrimaryButton("Apply", Cyan, {btnW, 32.f})) {
            if (host.onSave) host.onSave();
            if (host.onBack) host.onBack();
        }
        if (readOnly) ImGui::EndDisabled();
    }

    ImGui::EndChild();   // settings_card
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);

    ImGui::End();        // scrim
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(1);
}
