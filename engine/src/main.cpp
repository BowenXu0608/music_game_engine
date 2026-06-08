#include "engine/Engine.h"
#include "game/modes/Drop2DRenderer.h"
#include "game/chart/ChartLoader.h"
#include <iostream>
#include <filesystem>
#include <string>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#endif

// ── Test Game mode: full-screen game in its own window ──────────────────────
static int runTestGame(const std::string& projectPath) {
    // Engine is heap-allocated: it embeds the Renderer's two ParticleSystem
    // pools (std::array<Particle, 4096> each, ~800 KB total) by value, which
    // overflows the 1 MB thread stack if placed as a local.
    auto engine = std::make_unique<Engine>();
    try {
        engine->init(1280, 720, "Test Game", "shaders");

        // Load the project's material + particle-effect libraries (and migrate
        // charts). Without this the test process runs with empty libraries, so
        // note particles and the shared ui_tap button effect never resolve.
        engine->openProject(projectPath);

        // Load project into start screen and music selection
        engine->startScreenEditor().load(projectPath);
        engine->musicSelectionEditor().load(projectPath);

        // Enter test mode (full-screen game flow, no editor panels)
        engine->enterTestMode(EditorLayer::StartScreen);

        engine->runHub();

    } catch (const std::exception& e) {
        std::cerr << "[TestGame] Error: " << e.what() << "\n";
        return 1;
    }
    engine->shutdown();
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

    // Normal editor mode. Engine is heap-allocated for the same reason as the
    // test path: it embeds the Renderer's two ParticleSystem pools
    // (std::array<Particle, 4096> each, ~1 MB total) by value, which would
    // otherwise sit right at the edge of the 1 MB thread stack.
    auto engine = std::make_unique<Engine>();

    try {
        std::cout << "=== Music Game Engine Hub ===\n\n";
        engine->init(1600, 900, "Music Game Engine Hub", "shaders");

        engine->hub().setLaunchCallback([&](const ProjectInfo& proj) {
            std::cout << "Loading project: " << proj.name << "\n";

            std::string chartPath = proj.path + "/" + proj.defaultChart;
            if (std::filesystem::exists(chartPath)) {
                ChartData chart = ChartLoader::load(chartPath);
                std::string chartStem = std::filesystem::path(chartPath).stem().string();
                // main.cpp hub launch always hands the chart to Drop2DRenderer,
                // so use the Drop2D slot table / defaults for migration.
                engine->materialLibrary().migrateChartToAssets(chart, chartStem,
                                                               MaterialModeKey::Drop2D);
                Drop2DRenderer* renderer = new Drop2DRenderer();
                engine->setMode(renderer, chart);
            }
        });

        engine->runHub();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    engine->shutdown();
    return 0;
}
