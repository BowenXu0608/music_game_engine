#include "Widgets.h"
#include <imgui_internal.h>
#include <cstring>
#include <string>

namespace ui {

bool SectionHeader(const char* label, std::function<void()> rightSlot) {
    using namespace tokens;

    ImFont* labelFont = fonts.label ? fonts.label : ImGui::GetFont();
    const float labelFontSize = fonts.label ? fonts.label->FontSize : ImGui::GetFontSize() * 0.85f;
    const float letterSpacing = labelFontSize * 0.15f;

    const ImGuiStyle& style = ImGui::GetStyle();
    const float lineH = labelFontSize + style.FramePadding.y * 2.f + 4.f;
    const float fullW = ImGui::GetContentRegionAvail().x;
    const float pillMargin  = style.ItemSpacing.x;
    const float rightReserve = rightSlot ? (DefaultPillWidth() + pillMargin) : 0.f;

    ImGui::PushID(label);
    const ImVec2 cursorScreen = ImGui::GetCursorScreenPos();
    const float  startX = ImGui::GetCursorPosX();
    const float  startY = ImGui::GetCursorPosY();

    ImGui::Dummy(ImVec2(fullW, lineH));

    std::string upper;
    upper.reserve(std::strlen(label));
    for (const char* p = label; *p; ++p)
        upper += static_cast<char>((*p >= 'a' && *p <= 'z') ? (*p - 32) : *p);

    DrawSpacedText(ImGui::GetWindowDrawList(), labelFont, labelFontSize,
        {cursorScreen.x + 4.f,
         cursorScreen.y + (lineH - labelFontSize) * 0.5f},
        ToU32(TextLow), upper.c_str(), letterSpacing);

    if (rightSlot) {
        ImGui::SetCursorPos(ImVec2(startX + fullW - rightReserve, startY));
        rightSlot();
        ImGui::SetCursorPos(ImVec2(startX, startY + lineH + style.ItemSpacing.y));
    }

    ImGui::PopID();
    return true;
}

namespace {
constexpr float kDefaultPillFontScale = 0.72f;
constexpr float kDefaultPillSpacingEm = 0.08f;
constexpr const char* kDefaultPillLabel = "DEFAULT";

inline ImVec2 measureDefaultPillText() {
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * kDefaultPillFontScale;
    ImVec2 textSz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, kDefaultPillLabel);
    const int   nGaps = (int)std::strlen(kDefaultPillLabel) - 1;
    const float spacing = kDefaultPillSpacingEm * fontSize * (float)std::max(0, nGaps);
    return ImVec2(textSz.x + spacing, fontSize);
}
}

float DefaultPillWidth() {
    const ImVec2 t = measureDefaultPillText();
    return t.x;
}

bool DefaultPill(const char* id) {
    using namespace tokens;

    const ImVec2 t = measureDefaultPillText();
    const float pillW = t.x;
    const float pillH = ImGui::GetTextLineHeight();

    ImGui::PushID(id);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##defpill", ImVec2(pillW, pillH));
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    const ImU32 textCol = hovered ? ToU32(WithAlpha(Cyan, 0.75f)) : ToU32(Cyan);
    ImFont* font = ImGui::GetFont();
    const float fontSize = ImGui::GetFontSize() * kDefaultPillFontScale;
    const float gap = kDefaultPillSpacingEm * fontSize;
    float cx = origin.x;
    const float cy = origin.y + (pillH - fontSize) * 0.5f;
    for (const char* p = kDefaultPillLabel; *p; ++p) {
        const char ch[2] = {*p, 0};
        dl->AddText(font, fontSize, ImVec2(cx, cy), textCol, ch);
        ImVec2 cs = font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, ch);
        cx += cs.x + gap;
    }

    return clicked;
}

void Pill(const char* label, const ImVec4& color, bool solid) {
    using namespace tokens;

    std::string upper;
    for (const char* p = label; *p; ++p)
        upper += static_cast<char>((*p >= 'a' && *p <= 'z') ? (*p - 32) : *p);

    ImFont* font = fonts.label ? fonts.label : ImGui::GetFont();
    const float fontSize = font->FontSize;
    const float spacing = fontSize * 0.04f;
    const float textW = CalcSpacedTextWidth(font, fontSize, upper.c_str(), spacing);
    const float padX = 8.f, padY = 3.f;
    const float pillW = textW + padX * 2.f;
    const float pillH = fontSize + padY * 2.f;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(label, {pillW, pillH});
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const ImVec4 bg = solid ? color : WithAlpha(color, 0.16f);
    const ImVec4 fg = solid ? ImVec4{0.f, 0.f, 0.f, 1.f} : color;
    const ImVec4 br = solid ? color : WithAlpha(color, 0.40f);
    const ImVec2 pMax = {origin.x + pillW, origin.y + pillH};

    dl->AddRectFilled(origin, pMax, ToU32(bg), 999.f);
    dl->AddRect(origin, pMax, ToU32(br), 999.f);
    DrawSpacedText(dl, font, fontSize,
        {origin.x + padX, origin.y + padY},
        ToU32(fg), upper.c_str(), spacing);
}

bool LabeledChip(const char* label, int keyHint, const ImVec4& accent, bool active) {
    using namespace tokens;

    const ImGuiStyle& style = ImGui::GetStyle();
    const float fontH = ImGui::GetFontSize();
    const float chipH = 26.f;
    const float swatchSize = 14.f;

    char hintBuf[8] = {0};
    if (keyHint > 0) snprintf(hintBuf, sizeof(hintBuf), "%d", keyHint);
    const float hintW = (keyHint > 0)
        ? ImGui::CalcTextSize(hintBuf).x + 8.f
        : 0.f;

    const float labelW = ImGui::CalcTextSize(label).x;
    const float chipW =
        10.f /*pad*/ + swatchSize + 6.f /*gap*/ + labelW
        + (keyHint > 0 ? 6.f + hintW : 0.f) + 10.f /*pad*/;

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##chip", {chipW, chipH});
    const bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 chipMin = cursor;
    const ImVec2 chipMax = {cursor.x + chipW, cursor.y + chipH};

    const ImVec4 bg     = active ? WithAlpha(accent, 0.18f)
                          : (hovered ? WithAlpha(accent, 0.08f) : ImVec4{0,0,0,0});
    const ImVec4 border = active ? accent : Border;
    const ImVec4 text   = active ? accent : (hovered ? TextHi : TextMid);

    if (bg.w > 0.f)
        dl->AddRectFilled(chipMin, chipMax, ToU32(bg), 4.f);
    dl->AddRect(chipMin, chipMax, ToU32(border), 4.f);

    // Swatch.
    const ImVec2 swMin = {chipMin.x + 10.f, cursor.y + (chipH - swatchSize) * 0.5f};
    const ImVec2 swMax = {swMin.x + swatchSize, swMin.y + swatchSize};
    dl->AddRectFilled(swMin, swMax,
                      ToU32(active ? accent : WithAlpha(accent, 0.22f)), 3.f);
    dl->AddRect(swMin, swMax,
                ToU32(active ? accent : WithAlpha(accent, 0.50f)), 3.f);

    // Label.
    const ImVec2 labelPos = {swMax.x + 6.f, cursor.y + (chipH - fontH) * 0.5f};
    dl->AddText(labelPos, ToU32(text), label);

    // Key hint badge (mono style — boxed digit).
    if (keyHint > 0) {
        const ImVec2 hintMin = {labelPos.x + labelW + 6.f, cursor.y + (chipH - fontH * 0.85f) * 0.5f};
        const ImVec2 hintMax = {hintMin.x + hintW, hintMin.y + fontH * 0.85f};
        dl->AddRect(hintMin, hintMax, ToU32(WithAlpha(text, 0.55f)), 2.f);
        const ImVec2 hintTextPos = {hintMin.x + 4.f, hintMin.y + 1.f};
        dl->AddText(nullptr, fontH * 0.78f, hintTextPos,
                    ToU32(WithAlpha(text, 0.85f)), hintBuf);
    }

    return clicked;
}

bool SegBar(const char* id,
            const char* const items[][2],
            int itemCount,
            const char** current) {
    using namespace tokens;
    ImGui::PushID(id);

    const float fullW = ImGui::GetContentRegionAvail().x;
    const float h = 26.f;
    const float pad = 2.f;
    const float segW = (fullW - pad * 2.f) / static_cast<float>(itemCount);

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##segbg", {fullW, h});
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background.
    dl->AddRectFilled(cursor, {cursor.x + fullW, cursor.y + h}, ToU32(BgPanel), 4.f);
    dl->AddRect(cursor, {cursor.x + fullW, cursor.y + h}, ToU32(Border), 4.f);

    bool changed = false;
    for (int i = 0; i < itemCount; ++i) {
        const ImVec2 segMin = {cursor.x + pad + segW * i, cursor.y + pad};
        const ImVec2 segMax = {segMin.x + segW, cursor.y + h - pad};
        const bool isActive = (*current && std::strcmp(*current, items[i][0]) == 0);
        const bool inside = ImGui::IsMouseHoveringRect(segMin, segMax);

        if (isActive)
            dl->AddRectFilled(segMin, segMax, ToU32(BgPanel3), 3.f);
        else if (inside)
            dl->AddRectFilled(segMin, segMax, ToU32(WithAlpha(Cyan, 0.10f)), 3.f);

        const ImVec4 textColor = isActive ? TextHi : (inside ? TextMid : TextLow);
        const float labelW = ImGui::CalcTextSize(items[i][1]).x;
        dl->AddText({segMin.x + (segW - labelW) * 0.5f,
                     segMin.y + (h - pad * 2.f - ImGui::GetFontSize()) * 0.5f},
                    ToU32(textColor), items[i][1]);

        if (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !isActive) {
            *current = items[i][0];
            changed = true;
        }
    }

    ImGui::PopID();
    return changed;
}

bool Dropdown(const char* id,
              const char* const items[][2],
              int itemCount,
              const char** current) {
    using namespace tokens;
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        BgPanel);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, BgPanel2);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  BgPanel3);
    ImGui::PushStyleColor(ImGuiCol_Text,           TextHi);
    ImGui::PushStyleColor(ImGuiCol_Border,         BorderHi);
    ImGui::PushStyleColor(ImGuiCol_PopupBg,        BgPanel);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  WithAlpha(Cyan, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,   WithAlpha(Cyan, 0.22f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   4.f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,   4.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {8.f, 5.f});

    const char* preview = "-";
    int curIdx = -1;
    if (current && *current) {
        for (int i = 0; i < itemCount; ++i) {
            if (std::strcmp(*current, items[i][0]) == 0) {
                preview = items[i][1];
                curIdx = i;
                break;
            }
        }
    }

    bool changed = false;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo("##dd", preview, ImGuiComboFlags_HeightRegular)) {
        for (int i = 0; i < itemCount; ++i) {
            const bool isSel = (i == curIdx);
            if (ImGui::Selectable(items[i][1], isSel)) {
                if (current) {
                    *current = items[i][0];
                    changed = true;
                }
            }
            if (isSel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(8);
    ImGui::PopID();
    return changed;
}

static ImVec4 dimForAccent(const ImVec4& accent) {
    using namespace tokens;
    if (accent.x == Cyan.x && accent.y == Cyan.y)       return CyanDim;
    if (accent.x == Magenta.x && accent.y == Magenta.y) return MagentaDim;
    if (accent.x == Lime.x && accent.y == Lime.y)       return LimeDim;
    if (accent.x == Amber.x && accent.y == Amber.y)     return AmberDim;
    if (accent.x == Red.x && accent.y == Red.y)         return WithAlpha(Red, 0.5f);
    if (accent.x == Violet.x && accent.y == Violet.y)   return WithAlpha(Violet, 0.5f);
    return WithAlpha(accent, 0.5f);
}

bool Slider(const char* label,
            float* v,
            float vMin,
            float vMax,
            const char* suffix,
            const ImVec4& accent) {
    using namespace tokens;
    ImGui::PushID(label);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float fullW = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rowH    = ImGui::GetTextLineHeight();

    if (label && *label && label[0] != '#') {
        dl->AddText(origin, ToU32(TextMid), label);
        char buf[48];
        if (suffix && *suffix)
            snprintf(buf, sizeof(buf), "%.2f%s", *v, suffix);
        else
            snprintf(buf, sizeof(buf), "%.2f", *v);
        ImFont* mono = fonts.mono ? fonts.mono : ImGui::GetFont();
        const float monoSize = mono->FontSize;
        ImVec2 bsz = mono->CalcTextSizeA(monoSize, FLT_MAX, 0.f, buf);
        dl->AddText(mono, monoSize,
                    {origin.x + fullW - bsz.x, origin.y + (rowH - monoSize) * 0.5f},
                    ToU32(TextHi), buf);
    }

    const float trackY = origin.y + rowH + 4.f;
    const float trackH = 4.f;
    const ImVec2 trackMin = {origin.x, trackY};
    const ImVec2 trackMax = {origin.x + fullW, trackY + trackH};

    ImGui::SetCursorScreenPos({origin.x, origin.y});
    ImGui::InvisibleButton("##slider", {fullW, rowH + 4.f + trackH + 8.f});
    const bool hovered = ImGui::IsItemHovered();
    const bool active  = ImGui::IsItemActive();
    bool changed = false;
    if (active) {
        const float mx = ImGui::GetIO().MousePos.x;
        const float t  = std::max(0.f, std::min(1.f, (mx - trackMin.x) / fullW));
        const float nv = vMin + t * (vMax - vMin);
        if (nv != *v) { *v = nv; changed = true; }
    }

    dl->AddRectFilled(trackMin, trackMax, ToU32(BgPanel3), trackH * 0.5f);
    const float pct = (vMax > vMin) ? std::max(0.f, std::min(1.f,
        (*v - vMin) / (vMax - vMin))) : 0.f;
    const float fillEnd = trackMin.x + fullW * pct;
    if (fillEnd > trackMin.x + 0.5f) {
        const ImVec4 dimAccent = dimForAccent(accent);
        dl->AddRectFilled(
            {trackMin.x, trackMin.y - 1.f},
            {fillEnd, trackMax.y + 1.f},
            ToU32(WithAlpha(accent, 0.12f)), trackH);
        dl->AddRectFilledMultiColor(trackMin, {fillEnd, trackMax.y},
            ToU32(dimAccent), ToU32(accent), ToU32(accent), ToU32(dimAccent));
    }

    const ImVec2 thumb = {fillEnd, (trackMin.y + trackMax.y) * 0.5f};
    const float thumbR = active ? 6.5f : (hovered ? 6.f : 5.f);
    dl->AddCircleFilled(thumb, thumbR, IM_COL32(255, 255, 255, 255));

    ImGui::PopID();
    return changed;
}

bool SegBar(const char* id,
            std::initializer_list<const char*> labels,
            int* current) {
    using namespace tokens;
    ImGui::PushID(id);

    const int n = static_cast<int>(labels.size());
    const float fullW = ImGui::GetContentRegionAvail().x;
    const float h = 26.f;
    const float pad = 2.f;
    const float segW = (fullW - pad * 2.f) / static_cast<float>(n);

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##segbg_i", {fullW, h});
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(cursor, {cursor.x + fullW, cursor.y + h}, ToU32(BgPanel), 4.f);
    dl->AddRect(cursor, {cursor.x + fullW, cursor.y + h}, ToU32(Border), 4.f);

    bool changed = false;
    int i = 0;
    for (const char* label : labels) {
        const ImVec2 segMin = {cursor.x + pad + segW * i, cursor.y + pad};
        const ImVec2 segMax = {segMin.x + segW, cursor.y + h - pad};
        const bool isActive = (*current == i);
        const bool inside = ImGui::IsMouseHoveringRect(segMin, segMax);

        if (isActive)
            dl->AddRectFilled(segMin, segMax, ToU32(BgPanel3), 3.f);
        else if (inside)
            dl->AddRectFilled(segMin, segMax, ToU32(WithAlpha(Cyan, 0.10f)), 3.f);

        const ImVec4 textColor = isActive ? TextHi : (inside ? TextMid : TextLow);
        const float labelW = ImGui::CalcTextSize(label).x;
        dl->AddText({segMin.x + (segW - labelW) * 0.5f,
                     segMin.y + (h - pad * 2.f - ImGui::GetFontSize()) * 0.5f},
                    ToU32(textColor), label);

        if (inside && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !isActive) {
            *current = i;
            changed = true;
        }
        ++i;
    }

    ImGui::PopID();
    return changed;
}

// Pixel-fidelity TopBar matching MIGRATION mock §3 — flat 56 px bar:
//   [M tile] crumb1 > crumb2 ........ [Hub◀] [Music Selection▶] [▶ Test Game]
// The caller passes a rightSlot lambda; inside it the lambda calls
// ui::TopBarBack / Forward / TestGame helpers (or any custom buttons). We do
// NOT clip the rightSlot in a child window — the lambda is responsible for
// keeping its content within the bar height. Returns the y of the line below.
float TopBar(std::initializer_list<const char*> crumbs,
             std::function<void()> rightSlot) {
    using namespace tokens;
    const float toolbarH = 44.f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float fullW   = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddLine({origin.x, origin.y + toolbarH},
                {origin.x + fullW, origin.y + toolbarH},
                ToU32(Border), 1.f);

    // Solid gradient `M` tile (22x22) at left.
    const ImVec2 tileMin = {origin.x + 11.f, origin.y + 11.f};
    const ImVec2 tileMax = {tileMin.x + 22.f, tileMin.y + 22.f};
    dl->AddRectFilledMultiColor(tileMin, tileMax,
        ToU32(Cyan), ToU32(Magenta),
        ToU32(Magenta), ToU32(Cyan));
    dl->AddText(nullptr, ImGui::GetFontSize() * 0.95f,
                {tileMin.x + 6.f, tileMin.y + 4.f},
                IM_COL32(0, 0, 0, 255), "M");

    {
        float cx = tileMax.x + 12.f;
        const float cy = origin.y + 14.f;
        const ImU32 colHi  = ToU32(TextHi);
        const ImU32 colLow = ToU32(TextLow);
        const ImU32 colSep = ToU32(TextDim);
        int idx = 0;
        int total = (int)crumbs.size();
        for (auto* c : crumbs) {
            const bool last = (idx == total - 1);
            ImVec2 sz = ImGui::CalcTextSize(c);
            dl->AddText({cx, cy}, last ? colHi : colLow, c);
            cx += sz.x;
            if (!last) {
                cx += 10.f;
                dl->AddText({cx, cy}, colSep, ">");
                cx += ImGui::CalcTextSize(">").x + 10.f;
            }
            ++idx;
        }
    }

    // Right slot: anchor to right edge with no width clipping. The lambda is
    // expected to use SameLine() between consecutive items.
    if (rightSlot) {
        const float reserve = std::min(fullW * 0.55f, 540.f);
        const float rightX  = origin.x + fullW - reserve - 12.f;
        ImGui::SetCursorScreenPos({rightX, origin.y + 8.f});
        ImGui::PushID("##topbar_right");
        rightSlot();
        ImGui::PopID();
    }

    ImGui::SetCursorScreenPos({origin.x, origin.y + toolbarH});
    return origin.y + toolbarH;
}

static bool topNavBtn(const char* label, bool leftChevron, bool rightChevron) {
    using namespace tokens;
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(Cyan, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  WithAlpha(Cyan, 0.18f));
    ImGui::PushStyleColor(ImGuiCol_Text,          TextMid);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {12.f, 6.f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   4.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);

    char buf[64];
    snprintf(buf, sizeof(buf), "%s%s%s",
             leftChevron  ? "< " : "",
             label,
             rightChevron ? " >" : "");
    const bool clicked = ImGui::Button(buf);

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
    return clicked;
}

bool TopNavBack(const char* label)    { return topNavBtn(label, true,  false); }
bool TopNavForward(const char* label) { return topNavBtn(label, false, true ); }

bool TopToggle(const char* label, bool active) {
    using namespace tokens;
    const ImVec4 bg     = active ? WithAlpha(Cyan, 0.22f) : ImVec4(0, 0, 0, 0);
    const ImVec4 bgHov  = active ? WithAlpha(Cyan, 0.32f) : WithAlpha(Cyan, 0.10f);
    const ImVec4 bgAct  = WithAlpha(Cyan, 0.40f);
    const ImVec4 fg     = active ? Cyan : TextMid;
    const ImVec4 border = active ? Cyan : Border;

    ImGui::PushStyleColor(ImGuiCol_Button,        bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bgHov);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  bgAct);
    ImGui::PushStyleColor(ImGuiCol_Text,          fg);
    ImGui::PushStyleColor(ImGuiCol_Border,        border);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {10.f, 6.f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   4.f);

    const bool clicked = ImGui::Button(label);

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
    return clicked;
}

bool TopTestGame() {
    using namespace tokens;
    ImGui::PushStyleColor(ImGuiCol_Button,        Lime);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(Lime, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  WithAlpha(Lime, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.f, 0.10f, 0.04f, 1.f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {16.f, 7.f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   5.f);

    // Draw a play triangle prefix using the draw list so the label remains
    // pure ASCII (CP936-safe).
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::Button("    Test Game");
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cy = cursor.y + 7.f + ImGui::GetFontSize() * 0.5f;
    dl->AddTriangleFilled({cursor.x + 14.f, cy - 6.f},
                          {cursor.x + 14.f, cy + 6.f},
                          {cursor.x + 24.f, cy},
                          IM_COL32(0, 26, 10, 255));

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    return clicked;
}

bool Toggle(const char* label, bool* v, const ImVec4& accent) {
    using namespace tokens;
    ImGui::PushID(label);

    const float trackW = 36.f, trackH = 18.f;
    const float knobR  = 7.f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    bool changed = false;
    ImGui::InvisibleButton("##toggle", {trackW, trackH});
    if (ImGui::IsItemClicked()) { *v = !*v; changed = true; }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rounding = trackH * 0.5f;
    const ImVec4 bg = *v ? WithAlpha(accent, 0.35f) : BgPanel3;
    dl->AddRectFilled(origin, {origin.x + trackW, origin.y + trackH},
                      ToU32(bg), rounding);
    dl->AddRect(origin, {origin.x + trackW, origin.y + trackH},
                ToU32(*v ? accent : Border), rounding);

    const float knobX = *v ? (origin.x + trackW - knobR - 2.f)
                           : (origin.x + knobR + 2.f);
    const float knobY = origin.y + trackH * 0.5f;
    if (*v)
        dl->AddCircle({knobX, knobY}, knobR + 2.f, ToU32(WithAlpha(accent, 0.25f)), 0, 1.5f);
    dl->AddCircleFilled({knobX, knobY}, knobR,
                        ToU32(*v ? accent : TextMid));

    if (label && *label && label[0] != '#') {
        ImGui::SameLine(0.f, 8.f);
        ImGui::TextUnformatted(label);
    }

    ImGui::PopID();
    return changed;
}

bool Field(const char* label, char* buf, int bufSize, bool mono) {
    using namespace tokens;
    ImGui::PushID(label);

    if (label && *label && label[0] != '#') {
        if (fonts.label) ImGui::PushFont(fonts.label);
        ImGui::PushStyleColor(ImGuiCol_Text, TextMid);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        if (fonts.label) ImGui::PopFont();
    }

    ImGui::PushStyleColor(ImGuiCol_FrameBg,        BgPanel3);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, WithAlpha(Cyan, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  WithAlpha(Cyan, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_Border,         Border);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   5.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {8.f, 5.f});

    ImFont* inputFont = mono ? (fonts.monoSm ? fonts.monoSm : s_monoFont) : nullptr;
    if (inputFont) ImGui::PushFont(inputFont);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    bool changed = ImGui::InputText("##field", buf, bufSize);
    if (inputFont) ImGui::PopFont();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
    ImGui::PopID();
    return changed;
}

bool FieldFloat(const char* label, float* v, const char* format, bool mono) {
    using namespace tokens;
    ImGui::PushID(label);

    if (label && *label && label[0] != '#') {
        if (fonts.label) ImGui::PushFont(fonts.label);
        ImGui::PushStyleColor(ImGuiCol_Text, TextMid);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        if (fonts.label) ImGui::PopFont();
    }

    ImGui::PushStyleColor(ImGuiCol_FrameBg,        BgPanel3);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, WithAlpha(Cyan, 0.12f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  WithAlpha(Cyan, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_Border,         Border);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   5.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,    {8.f, 5.f});

    ImFont* inputFont = mono ? (fonts.monoSm ? fonts.monoSm : s_monoFont) : nullptr;
    if (inputFont) ImGui::PushFont(inputFont);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    bool changed = ImGui::InputFloat("##field", v, 0.f, 0.f, format);
    if (inputFont) ImGui::PopFont();

    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(4);
    ImGui::PopID();
    return changed;
}

void Placeholder(const char* label, float height, const ImVec4& accent) {
    using namespace tokens;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy({w, height});

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pMin = origin;
    const ImVec2 pMax = {origin.x + w, origin.y + height};

    const ImU32 stripeCol = ToU32(WithAlpha(accent, 0.06f));
    const float step = 12.f;
    dl->PushClipRect(pMin, pMax, true);
    for (float x = -height; x < w + height; x += step) {
        dl->AddLine({pMin.x + x, pMax.y},
                    {pMin.x + x + height, pMin.y},
                    stripeCol, 1.f);
    }
    dl->PopClipRect();

    dl->AddRect(pMin, pMax, ToU32(WithAlpha(accent, 0.25f)), 5.f, 0, 1.f);

    if (label && *label) {
        const ImVec2 ts = ImGui::CalcTextSize(label);
        dl->AddText({origin.x + (w - ts.x) * 0.5f,
                     origin.y + (height - ts.y) * 0.5f},
                    ToU32(WithAlpha(accent, 0.45f)), label);
    }
}

// ── Letter-spacing text helpers ──────────────────────────────────────────────

void DrawSpacedText(ImDrawList* dl, ImFont* font, float fontSize,
                    ImVec2 pos, ImU32 col, const char* text, float extraSpacing) {
    if (!font) font = ImGui::GetFont();
    if (fontSize <= 0.f) fontSize = font->FontSize;
    const float scale = fontSize / font->FontSize;
    float x = pos.x;
    const char* p = text;
    while (*p) {
        unsigned int c = (unsigned int)*p;
        const ImFontGlyph* glyph = font->FindGlyph((ImWchar)c);
        if (glyph) {
            dl->AddText(font, fontSize, {x, pos.y}, col, p, p + 1);
            x += glyph->AdvanceX * scale + extraSpacing;
        }
        ++p;
    }
}

float CalcSpacedTextWidth(ImFont* font, float fontSize, const char* text, float extraSpacing) {
    if (!font) font = ImGui::GetFont();
    if (fontSize <= 0.f) fontSize = font->FontSize;
    const float scale = fontSize / font->FontSize;
    float w = 0.f;
    int len = 0;
    for (const char* p = text; *p; ++p, ++len) {
        const ImFontGlyph* glyph = font->FindGlyph((ImWchar)(unsigned char)*p);
        if (glyph) w += glyph->AdvanceX * scale;
    }
    if (len > 1) w += extraSpacing * (float)(len - 1);
    return w;
}

// ── Button style helpers ────────────────────────────────────────────────────

bool PrimaryButton(const char* label, const ImVec4& accent, ImVec2 size) {
    using namespace tokens;
    ImGui::PushStyleColor(ImGuiCol_Button,        WithAlpha(accent, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   accent);
    ImGui::PushStyleColor(ImGuiCol_Text,           ImVec4(0, 0.05f, 0.08f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.f);
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    return clicked;
}

bool GhostButton(const char* label, ImVec2 size) {
    using namespace tokens;
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  BgPanel2);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   BgPanel3);
    ImGui::PushStyleColor(ImGuiCol_Text,           TextMid);
    ImGui::PushStyleColor(ImGuiCol_Border,         BorderHi);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,   5.f);
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(5);
    return clicked;
}

bool IconButton(const char* id, ImVec2 size,
                std::function<void(ImDrawList*, ImVec2, float, ImU32)> drawIcon) {
    using namespace tokens;
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,  WithAlpha(TextMid, 0.10f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,   WithAlpha(TextMid, 0.18f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    bool clicked = ImGui::Button(id, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
    if (drawIcon) {
        ImVec2 rMin = ImGui::GetItemRectMin();
        ImVec2 rMax = ImGui::GetItemRectMax();
        ImVec2 center = {(rMin.x + rMax.x) * 0.5f, (rMin.y + rMax.y) * 0.5f};
        float sz = std::min(size.x, size.y) * 0.4f;
        ImU32 col = ToU32(ImGui::IsItemHovered() ? TextHi : TextMid);
        drawIcon(ImGui::GetWindowDrawList(), center, sz, col);
    }
    return clicked;
}

} // namespace ui
