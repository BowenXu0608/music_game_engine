#include "StyleTokens.h"

void ApplyMusicGameStyle(ImGuiStyle& style) {
    using namespace ui::tokens;

    style.FrameRounding     = 4.f;
    style.GrabRounding      = 4.f;
    style.WindowRounding    = 6.f;
    style.PopupRounding     = 6.f;
    style.ScrollbarRounding = 6.f;
    style.TabRounding       = 4.f;
    style.ChildRounding     = 4.f;
    style.FrameBorderSize   = 0.f;
    style.WindowBorderSize  = 1.f;
    style.PopupBorderSize   = 1.f;

    style.WindowPadding   = {10.f, 10.f};
    style.FramePadding    = {8.f, 4.f};
    style.ItemSpacing     = {6.f, 4.f};
    style.ItemInnerSpacing= {4.f, 4.f};
    style.IndentSpacing   = 14.f;
    style.ScrollbarSize   = 10.f;
    style.GrabMinSize     = 10.f;

    ImVec4* c = style.Colors;

    // Mock §2 — pure black canvas with sub-step lift only on actual panel
    // chrome. WindowBg = void so editor-page backdrops match the React
    // mockup's #000 fill rather than the previous #070708 lift.
    c[ImGuiCol_WindowBg]           = BgVoid;
    c[ImGuiCol_ChildBg]            = BgPanel;
    c[ImGuiCol_PopupBg]            = BgPanel2;
    c[ImGuiCol_Border]             = Border;
    c[ImGuiCol_BorderShadow]       = {0.f, 0.f, 0.f, 0.f};

    c[ImGuiCol_FrameBg]            = BgPanel3;
    c[ImGuiCol_FrameBgHovered]     = WithAlpha(Cyan, 0.18f);
    c[ImGuiCol_FrameBgActive]      = WithAlpha(Cyan, 0.28f);

    c[ImGuiCol_TitleBg]            = BgPanel;
    c[ImGuiCol_TitleBgActive]      = BgPanel2;
    c[ImGuiCol_TitleBgCollapsed]   = BgBase;
    c[ImGuiCol_MenuBarBg]          = BgPanel;

    c[ImGuiCol_ScrollbarBg]        = {0.f, 0.f, 0.f, 0.f};
    c[ImGuiCol_ScrollbarGrab]      = WithAlpha(TextLow, 0.32f);
    c[ImGuiCol_ScrollbarGrabHovered] = WithAlpha(Cyan, 0.55f);
    c[ImGuiCol_ScrollbarGrabActive]  = Cyan;

    c[ImGuiCol_CheckMark]          = Cyan;
    c[ImGuiCol_SliderGrab]         = Cyan;
    c[ImGuiCol_SliderGrabActive]   = Magenta;

    c[ImGuiCol_Button]             = WithAlpha(Cyan, 0.18f);
    c[ImGuiCol_ButtonHovered]      = WithAlpha(Cyan, 0.55f);
    c[ImGuiCol_ButtonActive]       = Magenta;

    // Headers (CollapsingHeader / Selectable / TreeNode).
    c[ImGuiCol_Header]             = WithAlpha(BgPanel2, 1.f);
    c[ImGuiCol_HeaderHovered]      = WithAlpha(Cyan, 0.20f);
    c[ImGuiCol_HeaderActive]       = WithAlpha(Cyan, 0.36f);

    c[ImGuiCol_Separator]          = Divider;
    c[ImGuiCol_SeparatorHovered]   = WithAlpha(Cyan, 0.55f);
    c[ImGuiCol_SeparatorActive]    = Cyan;

    c[ImGuiCol_ResizeGrip]         = WithAlpha(Cyan, 0.18f);
    c[ImGuiCol_ResizeGripHovered]  = WithAlpha(Cyan, 0.65f);
    c[ImGuiCol_ResizeGripActive]   = Magenta;

    c[ImGuiCol_Tab]                = BgPanel;
    c[ImGuiCol_TabHovered]         = WithAlpha(Cyan, 0.30f);
    c[ImGuiCol_TabActive]          = WithAlpha(Cyan, 0.18f);
    c[ImGuiCol_TabUnfocused]       = BgBase;
    c[ImGuiCol_TabUnfocusedActive] = BgPanel;

    c[ImGuiCol_Text]               = TextHi;
    c[ImGuiCol_TextDisabled]       = TextLow;
    c[ImGuiCol_TextSelectedBg]     = WithAlpha(Cyan, 0.35f);

    c[ImGuiCol_PlotLines]          = Cyan;
    c[ImGuiCol_PlotLinesHovered]   = Magenta;
    c[ImGuiCol_PlotHistogram]      = Cyan;
    c[ImGuiCol_PlotHistogramHovered]= Magenta;

    c[ImGuiCol_DragDropTarget]     = Magenta;
    c[ImGuiCol_NavHighlight]       = WithAlpha(Cyan, 0.65f);
    c[ImGuiCol_ModalWindowDimBg]   = {0.f, 0.f, 0.f, 0.55f};
}
