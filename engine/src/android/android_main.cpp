// ============================================================================
// Android NativeActivity Entry Point
// Handles the Android app lifecycle and routes events to AndroidEngine.
// ============================================================================
#include "AndroidEngine.h"
#include "AndroidFileIO.h"
#include <android_native_app_glue.h>
#include <android/log.h>
#include <stdexcept>
#include <fstream>
#include <string>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "MusicGame", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MusicGame", __VA_ARGS__)

static AndroidEngine* g_engine = nullptr;

// Write crash message to internal storage so LauncherActivity can show it
static void writeCrashLog(android_app* app, const std::string& message) {
    std::string path = std::string(app->activity->internalDataPath) + "/crash.txt";
    std::ofstream f(path);
    if (f.is_open()) {
        f << message;
        f.close();
    }
    LOGE("Crash log written to: %s", path.c_str());
}

static void handleAppCmd(android_app* app, int32_t cmd) {
    if (!g_engine) return;

    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            LOGI("APP_CMD_INIT_WINDOW");
            if (app->window)
                g_engine->onWindowInit(app->window);
            break;

        case APP_CMD_TERM_WINDOW:
            LOGI("APP_CMD_TERM_WINDOW");
            g_engine->onWindowTerm();
            break;

        // The window's surface size or device configuration changed
        // (e.g. orientation flip from portrait to landscape after the
        // activity locks SCREEN_ORIENTATION_SENSOR_LANDSCAPE). We must
        // recreate the Vulkan swapchain to pick up the new ANativeWindow
        // dimensions, otherwise the framebuffer stays at the original
        // (often portrait) size.
        case APP_CMD_WINDOW_RESIZED:
            LOGI("APP_CMD_WINDOW_RESIZED");
            g_engine->onWindowResize();
            break;

        case APP_CMD_CONFIG_CHANGED:
            LOGI("APP_CMD_CONFIG_CHANGED");
            g_engine->onWindowResize();
            break;

        case APP_CMD_CONTENT_RECT_CHANGED:
            LOGI("APP_CMD_CONTENT_RECT_CHANGED");
            g_engine->onWindowResize();
            break;

        case APP_CMD_GAINED_FOCUS:
            LOGI("APP_CMD_GAINED_FOCUS");
            break;

        case APP_CMD_LOST_FOCUS:
            LOGI("APP_CMD_LOST_FOCUS");
            break;

        case APP_CMD_DESTROY:
            LOGI("APP_CMD_DESTROY");
            break;
    }
}

static int32_t handleInputEvent(android_app* app, AInputEvent* event) {
    if (!g_engine) return 0;

    int type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        g_engine->onTouchEvent(event);
        return 1;  // event handled
    }
    return 0;  // not handled
}

void android_main(android_app* app) {
    LOGI("android_main started");

    try {
        // Initialize file I/O with the app's asset manager and internal storage path
        AndroidFileIO::init(app->activity->assetManager,
                            app->activity->internalDataPath);

        // Set event handlers
        app->onAppCmd     = handleAppCmd;
        app->onInputEvent = handleInputEvent;

        // Create and initialize the engine
        AndroidEngine engine;
        g_engine = &engine;
        engine.init(app, "shaders");

        // Run the main loop (blocks until app exits)
        engine.mainLoop();

        // Clean up
        engine.shutdown();
        g_engine = nullptr;

        LOGI("android_main exiting");
    } catch (const std::exception& e) {
        LOGE("FATAL: %s", e.what());
        writeCrashLog(app, std::string("Native crash:\n") + e.what());
        g_engine = nullptr;
    } catch (...) {
        LOGE("FATAL: unknown exception");
        writeCrashLog(app, "Native crash:\nUnknown exception (no message available)");
        g_engine = nullptr;
    }
}
