#pragma once
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <mutex>

struct BpmChange {
    float time = 0.f;   // seconds
    float bpm  = 120.f;
};

struct AudioAnalysisResult {
    bool        success = false;
    std::string errorMessage;
    float       bpm = 0.f;                    // dominant BPM
    std::vector<BpmChange> bpmChanges;        // dynamic tempo map
    std::vector<float> easyMarkers;
    std::vector<float> mediumMarkers;
    std::vector<float> hardMarkers;
};

class AudioAnalyzer {
public:
    ~AudioAnalyzer();

    // Launch analysis in a background thread.
    void startAnalysis(const std::string& audioPath);

    bool isRunning()  const { return m_running.load(); }
    bool isComplete() const { return m_finished.load(); }

    // Call each frame from the UI thread. When analysis finishes,
    // invokes the callback set via setCallback() and resets state.
    void pollCompletion();

    void cancel();

    void setCallback(std::function<void(AudioAnalysisResult)> cb) {
        m_callback = std::move(cb);
    }

private:
    // Locate the analyzer: tries madmom_analyzer.exe first, then python script.
    std::string findAnalyzerExecutable() const;
    std::string findAnalyzerScript() const;

    // Run the subprocess, capture stdout, return parsed result.
    AudioAnalysisResult runSubprocess(const std::string& audioPath);

    // Parse JSON string into result struct.
    AudioAnalysisResult parseJson(const std::string& jsonStr);

    std::atomic<bool> m_running{false};
    std::atomic<bool> m_finished{false};
    std::thread       m_thread;
    std::mutex        m_resultMutex;
    AudioAnalysisResult m_result;
    std::function<void(AudioAnalysisResult)> m_callback;
};
