#include "engine/Engine.h"
#include "game/modes/BandoriRenderer.h"
#include "game/chart/ChartLoader.h"
#include <iostream>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif

int main() {
    // Anchor working directory to the exe's directory so relative paths
    // like "../../Projects" resolve correctly regardless of launch context.
#ifdef _WIN32
    {
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::filesystem::current_path(std::filesystem::path(exePath).parent_path());
    }
#endif
    Engine engine;

    try {
        std::cout << "=== Music Game Engine Hub ===\n\n";
        engine.init(1600, 900, "Music Game Engine Hub", "shaders");

        engine.hub().setLaunchCallback([&](const ProjectInfo& proj) {
            std::cout << "Loading project: " << proj.name << "\n";

            std::string chartPath = proj.path + "/" + proj.defaultChart;
            if (std::filesystem::exists(chartPath)) {
                ChartData chart = ChartLoader::load(chartPath);
                BandoriRenderer* renderer = new BandoriRenderer();
                engine.setMode(renderer, chart);
            }
        });

        engine.runHub();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    engine.shutdown();
    return 0;
}
