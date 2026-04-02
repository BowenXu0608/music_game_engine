#include <MusicGameEngine/Engine.h>
#include <MusicGameEngine/ChartLoader.h>
#include "BandoriRenderer.h"
#include <iostream>

int main() {
    try {
        Engine engine;
        engine.init(1280, 720, "Bandori Sandbox", "../../build/shaders");

        // Load demo chart
        ChartData chart = ChartLoader::load("assets/charts/demo.json");

        // Create and set game mode
        BandoriRenderer* renderer = new BandoriRenderer();
        engine.setMode(renderer, chart);

        std::cout << "=== Bandori Sandbox ===\n";
        std::cout << "Chart: " << chart.title << " - " << chart.artist << "\n";
        std::cout << "Notes: " << chart.notes.size() << "\n";

        engine.run();
        engine.shutdown();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
