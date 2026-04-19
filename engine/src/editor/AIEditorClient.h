#pragma once
#include "AIChatRequest.h"   // AIEditorResult + runChatRequest

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// Thin async wrapper around runChatRequest.
// Follows the same thread + pollCompletion pattern as AudioAnalyzer so the
// UI thread can poll once per frame and dispatch the completion callback on
// main, keeping Vulkan / ImGui off the worker.

class AIEditorClient {
public:
    ~AIEditorClient();

    // Kick off a request. Returns immediately; result delivered via
    // pollCompletion() + callback.
    void startRequest(const AIEditorConfig& cfg,
                      const std::string& systemPrompt,
                      const std::string& userPrompt);

    // Same as above but lets the caller opt out of JSON mode - used by
    // consumers that want prose responses (e.g. style-transfer narration).
    void startRequest(const AIEditorConfig& cfg,
                      const std::string& systemPrompt,
                      const std::string& userPrompt,
                      bool jsonMode);

    bool isRunning()  const { return m_running.load(); }
    bool isComplete() const { return m_finished.load(); }

    // Call each frame on the UI thread. If a result is ready, invokes the
    // callback set via setCallback() and joins the worker.
    void pollCompletion();

    void cancel();

    void setCallback(std::function<void(AIEditorResult)> cb) {
        m_callback = std::move(cb);
    }

private:
    std::atomic<bool>                      m_running{false};
    std::atomic<bool>                      m_finished{false};
    std::thread                            m_thread;
    std::mutex                             m_resultMutex;
    AIEditorResult                         m_result;
    std::function<void(AIEditorResult)>    m_callback;
};
