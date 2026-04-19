#include "AIEditorClient.h"

#include <iostream>

AIEditorClient::~AIEditorClient() {
    cancel();
}

void AIEditorClient::startRequest(const AIEditorConfig& cfg,
                                  const std::string& systemPrompt,
                                  const std::string& userPrompt) {
    startRequest(cfg, systemPrompt, userPrompt, /*jsonMode=*/true);
}

void AIEditorClient::startRequest(const AIEditorConfig& cfg,
                                  const std::string& systemPrompt,
                                  const std::string& userPrompt,
                                  bool jsonMode) {
    if (m_running.load()) return;

    m_finished.store(false);
    m_running.store(true);

    if (m_thread.joinable()) m_thread.join();
    AIEditorConfig cfgCopy = cfg;
    std::string sys = systemPrompt;
    std::string usr = userPrompt;
    bool jm = jsonMode;
    m_thread = std::thread([this, cfgCopy, sys, usr, jm]() {
        // Must not propagate - an uncaught exception here would hit
        // std::terminate (exit 3 on Windows). Convert anything to a
        // readable error so the UI can surface it.
        AIEditorResult result;
        try {
            result = runChatRequest(cfgCopy, sys, usr, jm);
        } catch (const std::exception& e) {
            result.errorMessage = std::string("Copilot worker exception: ") + e.what();
            std::cerr << "[AIEditorClient] " << result.errorMessage << "\n";
        } catch (...) {
            result.errorMessage = "Copilot worker threw an unknown exception";
            std::cerr << "[AIEditorClient] " << result.errorMessage << "\n";
        }
        {
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_result = std::move(result);
        }
        m_finished.store(true);
        m_running.store(false);
    });
}

void AIEditorClient::pollCompletion() {
    if (!m_finished.load()) return;

    m_finished.store(false);
    AIEditorResult result;
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        result = std::move(m_result);
    }

    if (m_callback) m_callback(std::move(result));

    if (m_thread.joinable()) m_thread.join();
}

void AIEditorClient::cancel() {
    m_running.store(false);
    m_finished.store(false);
    if (m_thread.joinable()) m_thread.join();
}
