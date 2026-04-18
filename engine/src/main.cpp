#include "engine/Engine.h"
#include "game/modes/BandoriRenderer.h"
#include "game/chart/ChartLoader.h"
#include <iostream>
#include <filesystem>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

// ── Test Game mode: full-screen game in its own window ──────────────────────
static int runTestGame(const std::string& projectPath) {
    Engine engine;
    try {
        engine.init(1280, 720, "Test Game", "shaders");

        // Load project into start screen and music selection
        engine.startScreenEditor().load(projectPath);
        engine.musicSelectionEditor().load(projectPath);

        // Enter test mode (full-screen game flow, no editor panels)
        engine.enterTestMode(EditorLayer::StartScreen);

        engine.runHub();

    } catch (const std::exception& e) {
        std::cerr << "[TestGame] Error: " << e.what() << "\n";
        return 1;
    }
    engine.shutdown();
    return 0;
}

// ── Normal editor mode ──────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    // Anchor working directory to the exe's directory
#ifdef _WIN32
    {
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::filesystem::current_path(std::filesystem::path(exePath).parent_path());
    }
#endif

    // Check for --test <project_path> argument
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--test" && i + 1 < argc) {
            return runTestGame(argv[i + 1]);
        }
    }

    // Normal editor mode
    Engine engine;

    try {
        std::cout << "=== Music Game Engine Hub ===\n\n";
        engine.init(1600, 900, "Music Game Engine Hub", "shaders");

        engine.hub().setLaunchCallback([&](const ProjectInfo& proj) {
            std::cout << "Loading project: " << proj.name << "\n";

            std::string chartPath = proj.path + "/" + proj.defaultChart;
            if (std::filesystem::exists(chartPath)) {
                ChartData chart = ChartLoader::load(chartPath);
                std::string chartStem = std::filesystem::path(chartPath).stem().string();
                // main.cpp hub launch always hands the chart to BandoriRenderer,
                // so use the Bandori slot table / defaults for migration.
                engine.materialLibrary().migrateChartToAssets(chart, chartStem,
                                                              MaterialModeKey::Bandori);
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
