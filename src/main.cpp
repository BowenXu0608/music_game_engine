#include "engine/Engine.h"
#include "game/chart/ChartTypes.h"
#include <iostream>
#include <cmath>

static constexpr float TWO_PI = 6.28318530717958f;

int main() {
    Engine engine;
    try {
        engine.init(1280, 720, "Music Game Engine", "shaders");

        // Bandori test chart with hit effects
        ChartData chart;
        chart.title = "Bandori Test";
        uint32_t id = 0;
        for (int i = 0; i < 28; ++i) {
            NoteEvent note{};
            note.time = 2.0 + i * 0.4;
            note.type = NoteType::Tap;
            note.id   = id++;
            note.data = TapData{ static_cast<float>(i % 7) };
            chart.notes.push_back(note);
        }
        for (int i = 0; i < 7; ++i) {
            NoteEvent hold{};
            hold.time = 2.0 + 28 * 0.4 + i * 0.5;
            hold.type = NoteType::Hold;
            hold.id   = id++;
            hold.data = HoldData{ static_cast<float>(i), 0.4f };
            chart.notes.push_back(hold);

            NoteEvent flick{};
            flick.time = 2.0 + 28 * 0.4 + i * 0.5 + 0.25;
            flick.type = NoteType::Flick;
            flick.id   = id++;
            flick.data = FlickData{ static_cast<float>(i), 1 };
            chart.notes.push_back(flick);
        }

        engine.setMode(GameMode::Bandori, chart);
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    engine.shutdown();
    return 0;
}
