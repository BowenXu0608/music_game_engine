#include "engine/Engine.h"
#include "game/chart/ChartTypes.h"
#include "ui/SceneViewer.h"
#include <iostream>

SceneViewer g_sceneViewer;

ChartData createBandoriDemo() {
    ChartData chart;
    chart.title = "BanG Dream Demo";
    uint32_t id = 0;

    for (int i = 0; i < 28; ++i) {
        NoteEvent note{};
        note.time = 2.0 + i * 0.4;
        note.type = NoteType::Tap;
        note.id = id++;
        note.data = TapData{ static_cast<float>(i % 7) };
        chart.notes.push_back(note);
    }

    for (int i = 0; i < 7; ++i) {
        NoteEvent hold{};
        hold.time = 13.0 + i * 0.5;
        hold.type = NoteType::Hold;
        hold.id = id++;
        hold.data = HoldData{ static_cast<float>(i), 0.4f };
        chart.notes.push_back(hold);
    }

    return chart;
}

int main() {
    Engine engine;

    try {
        std::cout << "=== Music Game Engine - Unity-Style Editor ===\n\n";

        engine.init(1600, 900, "Music Game Engine - Editor", "shaders");

        ChartData chart = createBandoriDemo();
        engine.setMode(GameMode::Bandori, chart);

        std::cout << "Editor started with " << chart.notes.size() << " notes\n";

        engine.run();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    engine.shutdown();
    return 0;
}
