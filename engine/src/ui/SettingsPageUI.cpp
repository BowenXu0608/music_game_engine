#include "SettingsPageUI.h"
#include "StyleTokens.h"
#include "Widgets.h"

#include "engine/AudioEngine.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>

namespace {

// Local state for the tap-to-calibrate wizard. Persists across ImGui frames
// because ImGui is immediate-mode: `static` keeps the captured tap samples
// alive while the user is tapping. Only one instance of the settings page is
// ever visible at a time in this engine, so a single static is safe.
struct CalibrationState {
    bool   active = false;
    double startTime = 0.0;    // ImGui::GetTime() when metronome started
    double beatPeriod = 1.0;   // seconds between beats
    int    beatsTotal = 4;
    int    beatsTicked = 0;    // how many clicks we've played
    std::vector<double> tapDeltas;  // (tapTime - nearestBeatTime) for each tap
};

static CalibrationState& calState() {
    static CalibrationState s;
    return s;
}

static const char* kLanguageChoices[] = {
    "en",   // English
    "zh",   // Chinese
    "ja",   // Japanese
    "ko",   // Korean
};

void drawHeader(const char* title, float width) {
    ImGui::SetWindowFontScale(1.6f);
    ImGui::TextColored(ui::tokens::TextHi, "%s", title);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SameLine(width - 100.f);
}

void sectionHeader(const char* label, const ImVec4& accent = ui::tokens::Cyan) {
    ImGui::Spacing();
    ImGui::SetWindowFontScale(1.05f);
    ImGui::TextColored(accent, "%s", label);
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PushStyleColor(ImGuiCol_Separator, ui::tokens::Divider);
    ImGui::Separator();
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void drawCalibrationPanel(PlayerSettings& s, AudioEngine* audio, bool readOnly) {
    auto& cs = calState();

    // When inactive — offer a Start button.
    if (!cs.active) {
        ImGui::TextWrapped(
            "Play a metronome and tap the screen on each beat. "
            "We'll compute the average offset from your taps.");
        ImGui::Spacing();
        const bool canCalibrate = (audio != nullptr) && !readOnly;
        if (!canCalibrate) ImGui::BeginDisabled();
        if (ImGui::Button("Start Calibration", ImVec2(-1, 40)) && canCalibrate) {
            cs.active = true;
            cs.startTime = ImGui::GetTime() + 1.0;  // 1 s lead-in before first beat
            cs.beatsTicked = 0;
            cs.tapDeltas.clear();
        }
        if (!canCalibrate) ImGui::EndDisabled();
        if (!audio && !readOnly)
            ImGui::TextDisabled("(Audio not available in this preview.)");
        return;
    }

    const double now = ImGui::GetTime();
    const double tSinceStart = now - cs.startTime;

    // Emit metronome ticks: one audible click per beat boundary we've crossed.
    while (cs.beatsTicked < cs.beatsTotal &&
           tSinceStart >= cs.beatsTicked * cs.beatPeriod) {
        if (audio) audio->playClickSfx();
        cs.beatsTicked++;
    }

    // Capture taps during the calibration window.
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        // nearest beat boundary in "time since start" domain
        const double beatIdx = std::round(tSinceStart / cs.beatPeriod);
        const double expectedT = beatIdx * cs.beatPeriod;
        const double dt = tSinceStart - expectedT;     // positive = tapped late
        // Only accept taps within half a beat period — ignore stray clicks.
        if (std::abs(dt) < cs.beatPeriod * 0.5 && beatIdx >= 0 && beatIdx < cs.beatsTotal) {
            cs.tapDeltas.push_back(dt);
        }
    }

    // Progress line.
    ImGui::Text("Beat %d / %d", cs.beatsTicked, cs.beatsTotal);
    ImGui::Text("Taps captured: %d", (int)cs.tapDeltas.size());

    // Once the metronome has finished and at least a second has passed, show
    // Accept / Retry / Cancel.
    const bool finished = (cs.beatsTicked >= cs.beatsTotal) &&
                          (tSinceStart > cs.beatsTotal * cs.beatPeriod + 0.5);
    if (finished) {
        double avgMs = 0.0;
        if (!cs.tapDeltas.empty()) {
            double sum = 0.0;
            for (double d : cs.tapDeltas) sum += d;
            avgMs = (sum / (double)cs.tapDeltas.size()) * 1000.0;
        }
        ImGui::Separator();
        ImGui::Text("Average tap offset: %+.1f ms", avgMs);
        if (ImGui::Button("Accept", ImVec2(120, 32))) {
            s.audioOffsetMs = static_cast<float>(avgMs);
            cs.active = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Retry", ImVec2(120, 32))) {
            cs.active = true;
            cs.startTime = ImGui::GetTime() + 1.0;
            cs.beatsTicked = 0;
            cs.tapDeltas.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 32))) {
            cs.active = false;
        }
    }
}

} // namespace

void SettingsPageUI::render(ImVec2          origin,
                            ImVec2          size,
                            PlayerSettings& s,
                            const Host&     host,
                            bool            readOnly)
{
    // The settings page uses a full-screen opaque window as a backdrop; the
    // card is rendered as a centered child region inside it. This guarantees
    // nothing from the caller's scene bleeds through.
    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,
        ui::tokens::WithAlpha(ui::tokens::BgVoid, 0.92f));
    ImGui::Begin("##settings_page_scrim", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoScrollbar);
    // Pin to top of z-stack every frame without resetting focus/active item
    // (SetNextWindowFocus would break in-progress slider drags).
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    // Centered card.
    const float cardW = std::min(size.x - 80.f, 640.f);
    const float cardH = std::min(size.y - 60.f, 720.f);
    const float cardX = (size.x - cardW) * 0.5f;
    const float cardY = (size.y - cardH) * 0.5f;

    ImGui::SetCursorPos(ImVec2(cardX, cardY));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 18));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10, 8));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ui::tokens::BgPanel);
    ImGui::PushStyleColor(ImGuiCol_Border,  ui::tokens::BorderHi);
    ImGui::BeginChild("##settings_card", ImVec2(cardW, cardH), true,
        ImGuiWindowFlags_NoScrollbar);

    // Header row: title on left, Cancel / Apply on right (MIGRATION §3.5).
    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextColored(ui::tokens::TextHi, "Settings");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SameLine();
    const float btnW   = 80.f;
    const float btnGap = 6.f;
    const float pairW  = btnW * 2.f + btnGap;
    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - pairW);

    using namespace ui::tokens;
    // Cancel — ghost button, no save, just back.
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, BgPanel2);
    ImGui::PushStyleColor(ImGuiCol_Text,          TextMid);
    ImGui::PushStyleColor(ImGuiCol_Border,        BorderHi);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    if (ImGui::Button("Cancel", ImVec2(btnW, 30))) {
        if (host.onBack) host.onBack();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);

    ImGui::SameLine(0.f, btnGap);

    // Apply — primary cyan glow, save then back.
    if (readOnly) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,        WithAlpha(Cyan, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Cyan);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  Cyan);
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0, 0.05f, 0.08f, 1));
    if (ImGui::Button("Apply", ImVec2(btnW, 30))) {
        if (host.onSave) host.onSave();
        if (host.onBack) host.onBack();
    }
    ImGui::PopStyleColor(4);
    if (readOnly) ImGui::EndDisabled();

    ImGui::Separator();

    if (readOnly) ImGui::BeginDisabled();

    // Scrollable body; slider labels stay aligned inside the card.
    ImGui::BeginChild("##settings_body", ImVec2(0, 0), false);

    // Constrain slider width so labels have room on the right.
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);

    // ── Audio ────────────────────────────────────────────────────────────────
    sectionHeader("AUDIO", ui::tokens::Cyan);

    ui::Slider("Music volume",     &s.musicVolume,    0.f, 1.f, "", ui::tokens::Cyan);
    ImGui::Spacing();
    ui::Slider("Hit-sound volume", &s.hitSoundVolume, 0.f, 1.f, "", ui::tokens::Cyan);
    ImGui::Spacing();
    ImGui::Checkbox("Hit-Sound Enabled", &s.hitSoundEnabled);

    if (!readOnly && host.audio) {
        host.audio->setMusicVolume(s.musicVolume);
        host.audio->setSfxVolume(s.hitSoundVolume);
        host.audio->setHitSoundEnabled(s.hitSoundEnabled);
    }

    ImGui::Spacing();
    ui::Slider("Audio offset", &s.audioOffsetMs, -200.f, 200.f, " ms", ui::tokens::Magenta);

    ImGui::Spacing();
    drawCalibrationPanel(s, host.audio, readOnly);

    // ── Gameplay ─────────────────────────────────────────────────────────────
    sectionHeader("GAMEPLAY", ui::tokens::Magenta);
    ui::Slider("Note speed", &s.noteSpeed, 1.f, 10.f, "", ui::tokens::Magenta);
    ImGui::TextDisabled("5 = default  |  Scan Line mode ignores this");

    // ── Visual ───────────────────────────────────────────────────────────────
    sectionHeader("VISUAL", ui::tokens::Lime);
    ui::Slider("Background dim", &s.backgroundDim, 0.f, 1.f, "", ui::tokens::Lime);
    ImGui::Spacing();
    ImGui::Checkbox("Show FPS Counter", &s.fpsCounter);

    // ── Misc ─────────────────────────────────────────────────────────────────
    sectionHeader("MISC", ui::tokens::Amber);
    int langIdx = 0;
    for (int i = 0; i < (int)(sizeof(kLanguageChoices)/sizeof(kLanguageChoices[0])); ++i) {
        if (s.language == kLanguageChoices[i]) { langIdx = i; break; }
    }
    if (ImGui::Combo("Language", &langIdx, kLanguageChoices,
                     (int)(sizeof(kLanguageChoices)/sizeof(kLanguageChoices[0])))) {
        s.language = kLanguageChoices[langIdx];
    }
    ImGui::TextDisabled("Stored only (localization not wired yet)");

    ImGui::PopItemWidth();
    ImGui::EndChild();

    if (readOnly) ImGui::EndDisabled();

    ImGui::EndChild();   // close settings_card
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);

    ImGui::End();        // close scrim window
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(1);
}
