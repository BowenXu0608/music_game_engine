#pragma once
#include <imgui.h>

class IPlayerEngine;

// End-of-song results overlay: score, max combo, judgment breakdown, back
// button. Stateless — pulls all data from IPlayerEngine each frame.
class ResultsView {
public:
    void render(ImVec2 displaySize, IPlayerEngine& engine);
};
