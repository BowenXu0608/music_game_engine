// Android-only stub for ImGuiLayer methods referenced from shared UI/view
// code. Android passes m_imgui = nullptr to all views, so these symbols
// exist purely to satisfy the linker — they are never called.
#include "ui/ImGuiLayer.h"
#include <imgui.h>

ImFont* ImGuiLayer::getLogoFont(float) const {
    return ImGui::GetFont();
}
