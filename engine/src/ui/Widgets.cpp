#include "Widgets.h"
#include <imgui_internal.h>
#include <cstring>
#include <string>

namespace ui {

bool SectionHeader(const char* label, std::function<void()> rightSlot) {
    using namespace tokens;

    ImGuiStorage* storage = ImGui::GetStateStorage();
    const ImGuiID stateId = ImGui::GetID(label);
    bool open = storage->GetBool(stateId, true);

    const ImGuiStyle& style = ImGui::GetStyle();
    const float lineH = ImGui::GetTextLineHeight() + style.FramePadding.y * 2.f;
    const float fullW = ImGui::GetContentRegionAvail().x;

    ImGui::PushID(label);
    ImVec2 cursor = ImGui::GetCursorScreenPos();

    // Invisible button covers the chevron + label area only (not the right slot)
    // so a Default-pill click doesn't toggle the section.
    const float rightReserve = rightSlot ? 60.f : 0.f;
    const float toggleW = fullW - rightReserve;
    if (ImGui::InvisibleButton("##hdr_toggle", {toggleW, lineH})) {
        open = !open;
        storage->SetBool(stateId, open);
    }
    const bool hovered = ImGui::IsItemHovered();

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Chevron.
    const float chevSize = 6.f;
    const ImVec2 chevCenter{cursor.x + 6.f, cursor.y + lineH * 0.5f};
    const ImU32 chevColor = ToU32(hovered ? TextMid : TextLow);
    if (open) {
        // Down-pointing chevron.
        dl->AddTriangleFilled(
            ImVec2(chevCenter.x - chevSize, chevCenter.y - chevSize * 0.5f),
            ImVec2(chevCenter.x + chevSize, chevCenter.y - chevSize * 0.5f),
            ImVec2(chevCenter.x,            chevCenter.y + chevSize * 0.6f),
            chevColor);
    } else {
        // Right-pointing chevron.
        dl->AddTriangleFilled(
            ImVec2(chevCenter.x - chevSize * 0.5f, chevCenter.y - chevSize),
            ImVec2(chevCenter.x - chevSize * 0.5f, chevCenter.y + chevSize),
            ImVec2(chevCenter.x + chevSize * 0.6f, chevCenter.y),
            chevColor);
    }

    // Uppercase label, with letter-spacing approximated by extra char width.
    std::string upper;
    upper.reserve(std::strlen(label));
    for (const char* p = label; *p; ++p)
        upper += static_cast<char>((*p >= 'a' && *p <= 'z') ? (*p - 32) : *p);
    const ImU32 textColor = ToU32(hovered ? TextMid : TextLow);
    dl->AddText(nullptr, ImGui::GetFontSize() * 0.85f,
                {chevCenter.x + 12.f, cursor.y + style.FramePadding.y * 0.5f + 2.f},
                textColor, upper.c_str());

    // Right slot: render on the same row, right-aligned.
    if (rightSlot) {
        ImGui::SameLine(0.f, 0.f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (fullW - rightReserve));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - lineH);
        rightSlot();
    }

    ImGui::PopID();
    return open;
}

bool DefaultPill(const char* id) {
    using namespace tokens;
    ImGui::PushStyleColor(ImGuiCol_Button,        WithAlpha(Cyan, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, WithAlpha(Cyan, 0.18f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  WithAlpha(Cyan, 0.36f));
    ImGui::PushStyleColor(ImGuiCol_Text,          Cyan);
    ImGui::PushStyleColor(ImGuiCol_Border,        BorderHi);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  {6.f, 1.f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f);
    const bool clicked = ImGui::Button((std::string("DEFAULT##") + id).c_str());
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
    return clicked;
}

void Pill(const char* label, const ImVec4& color, bool solid) {
    using namespace tokens;
    const ImVec4 bg = solid ? color : WithAlpha(color, 0.16f);
    const ImVec4 fg = solid ? ImVec4{0.f, 0.f, 0.f, 1.f} : color;
    const ImVec4 br = solid ? color : WithAlpha(color, 0.40f);

    ImGui::PushStyleColor(ImGuiCol_Button,        bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, bg);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  bg);
    ImGui::PushStyleColor(ImGuiCol_Text,          fg);
    ImGui::PushStyleColor(ImGuiCol_Border,        br);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  {8.f, 2.f});
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 999.f);
    ImGui::SmallButton(label);
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(5);
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

bool Slider(const char* label,
            float* v,
            float vMin,
            float vMax,
            const char* suffix,
            const ImVec4& accent) {
    using namespace tokens;
    ImGui::PushID(label);

    // Two-row layout: top label + mono right-aligned value, bottom 6 px track
    // with accent fill from min to current value. Click/drag the track to
    // change value (matches ImGui::SliderFloat behavior under the hood).
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float fullW = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rowH    = ImGui::GetTextLineHeight();

    // Label row.
    if (label && *label && label[0] != '#') {
        dl->AddText(origin, ToU32(TextMid), label);
        char buf[48];
        if (suffix && *suffix)
            snprintf(buf, sizeof(buf), "%.2f%s", *v, suffix);
        else
            snprintf(buf, sizeof(buf), "%.2f", *v);
        const float bw = ImGui::CalcTextSize(buf).x;
        ImFont* mono = s_monoFont ? s_monoFont : ImGui::GetFont();
        dl->AddText(mono, ImGui::GetFontSize(),
                    {origin.x + fullW - bw, origin.y},
                    ToU32(TextHi), buf);
    }

    // Track.
    const float trackY = origin.y + rowH + 4.f;
    const float trackH = 6.f;
    const ImVec2 trackMin = {origin.x, trackY};
    const ImVec2 trackMax = {origin.x + fullW, trackY + trackH};

    // Hit area covers a wider zone so the slider is comfortable to grab.
    const ImVec2 hitMin = {origin.x, trackY - 5.f};
    const ImVec2 hitMax = {origin.x + fullW, trackY + trackH + 5.f};
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

    // Draw track + accent fill + thumb.
    dl->AddRectFilled(trackMin, trackMax, ToU32(BgPanel3), trackH * 0.5f);
    dl->AddRect(trackMin, trackMax, ToU32(Border), trackH * 0.5f);
    const float pct = (vMax > vMin) ? std::max(0.f, std::min(1.f,
        (*v - vMin) / (vMax - vMin))) : 0.f;
    const float fillEnd = trackMin.x + fullW * pct;
    if (fillEnd > trackMin.x + 0.5f) {
        dl->AddRectFilled(trackMin, {fillEnd, trackMax.y},
                          ToU32(accent), trackH * 0.5f);
    }
    const ImVec2 thumb = {fillEnd, (trackMin.y + trackMax.y) * 0.5f};
    const float thumbR = active ? 7.f : (hovered ? 6.f : 5.f);
    dl->AddCircleFilled(thumb, thumbR + 2.f, ToU32(WithAlpha(accent, 0.25f)));
    dl->AddCircleFilled(thumb, thumbR, IM_COL32(255, 255, 255, 255));
    dl->AddCircle(thumb, thumbR, ToU32(accent), 0, 1.5f);

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
    const float toolbarH = 56.f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float fullW   = ImGui::GetContentRegionAvail().x;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Pure-black bar — let the void window underneath show through. Only
    // the thin bottom rule separates it from the body chrome below.
    dl->AddLine({origin.x, origin.y + toolbarH},
                {origin.x + fullW, origin.y + toolbarH},
                ToU32(Border), 1.f);

    // Solid gradient `M` tile (28x28) at left.
    const ImVec2 tileMin = {origin.x + 14.f, origin.y + 14.f};
    const ImVec2 tileMax = {tileMin.x + 28.f, tileMin.y + 28.f};
    dl->AddRectFilledMultiColor(tileMin, tileMax,
        ToU32(Cyan), ToU32(Magenta),
        ToU32(Magenta), ToU32(Cyan));
    dl->AddText(nullptr, ImGui::GetFontSize() * 1.05f,
                {tileMin.x + 8.f, tileMin.y + 6.f},
                IM_COL32(0, 0, 0, 255), "M");

    // Crumbs — drawn directly with ImDrawList so they're not affected by
    // ImGui::Item layout / clipping. Last crumb is white, rest are TextLow.
    {
        float cx = tileMax.x + 14.f;
        const float cy = origin.y + 18.f;
        const ImU32 colHi  = ToU32(TextHi);
        const ImU32 colLow = ToU32(TextLow);
        const ImU32 colSep = ToU32(TextLow);
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
        ImGui::SetCursorScreenPos({rightX, origin.y + 12.f});
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

} // namespace ui
