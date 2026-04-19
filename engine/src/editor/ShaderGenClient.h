#pragma once
#include "AIChatRequest.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// Async AI shader generator.
//
// Kicks off an OpenAI-compatible chat request asking for a full Vulkan/GLSL
// 450 fragment shader, writes the response to `outFragPath`, then invokes
// glslc via ShaderCompiler. On compile failure the worker retries (default
// 3 attempts) feeding the compile errors back to the model so it can fix
// them. On success, the `.spv` produced by glslc can be picked up directly
// by the batcher's custom pipeline cache (same as the existing Compile
// button in the Materials tab).

struct ShaderGenResult {
    bool        success     = false;
    std::string spvPath;       // compiled .spv on success
    std::string fragSource;    // final .frag source on success
    std::string attemptsLog;   // per-attempt OK / error summary
    int         attempts     = 0;  // 1-based count of attempts consumed
    std::string errorMessage;  // human-readable reason on failure
};

class ShaderGenClient {
public:
    struct Status {
        int         attempt     = 0;  // 1-based; 0 = not started
        int         maxAttempts = 0;
        std::string phase;            // "sending" / "writing" / "compiling" / "retrying"
    };

    ~ShaderGenClient();

    // outFragPath is where the generated .frag will be written. Existing
    // content is overwritten. maxAttempts clamped to >=1.
    void startGeneration(const AIEditorConfig& cfg,
                          const std::string& userPrompt,
                          const std::string& outFragPath,
                          int maxAttempts = 3);

    bool isRunning()  const { return m_running.load(); }
    bool isComplete() const { return m_finished.load(); }

    // Call each frame on the UI thread. If a result is ready, invokes the
    // callback set via setCallback() and joins the worker.
    void pollCompletion();
    void cancel();

    void setCallback(std::function<void(ShaderGenResult)> cb) {
        m_callback = std::move(cb);
    }

    // In-progress status snapshot safe to read from the UI thread.
    Status liveStatus() const {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        return m_liveStatus;
    }

private:
    ShaderGenResult runLoop(const AIEditorConfig& cfg,
                             const std::string& userPrompt,
                             const std::string& outFragPath,
                             int maxAttempts);

    std::atomic<bool>                     m_running{false};
    std::atomic<bool>                     m_finished{false};
    std::thread                           m_thread;
    std::mutex                            m_resultMutex;
    ShaderGenResult                       m_result;
    std::function<void(ShaderGenResult)>  m_callback;

    mutable std::mutex                    m_statusMutex;
    Status                                m_liveStatus;
};
