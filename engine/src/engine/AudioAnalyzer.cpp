#include "AudioAnalyzer.h"
#include <iostream>
#include <sstream>
#include <filesystem>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

AudioAnalyzer::~AudioAnalyzer() {
    cancel();
}

void AudioAnalyzer::startAnalysis(const std::string& audioPath) {
    if (m_running.load()) return;

    // Reset state
    m_finished.store(false);
    m_running.store(true);

    // Launch background thread
    if (m_thread.joinable()) m_thread.join();
    m_thread = std::thread([this, audioPath]() {
        auto result = runSubprocess(audioPath);
        {
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_result = std::move(result);
        }
        m_finished.store(true);
        m_running.store(false);
    });
}

void AudioAnalyzer::pollCompletion() {
    if (!m_finished.load()) return;

    m_finished.store(false);
    AudioAnalysisResult result;
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        result = std::move(m_result);
    }

    if (m_callback) m_callback(std::move(result));

    if (m_thread.joinable()) m_thread.join();
}

void AudioAnalyzer::cancel() {
    // TODO: kill subprocess if needed
    m_running.store(false);
    m_finished.store(false);
    if (m_thread.joinable()) m_thread.join();
}

// ── Locate the analyzer tool ────────────────────────────────────────────────

std::string AudioAnalyzer::findAnalyzerExecutable() const {
#ifdef _WIN32
    // Look relative to our own .exe
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();

    // Check for standalone PyInstaller .exe
    fs::path bundled = exeDir / "tools" / "madmom_analyzer.exe";
    if (fs::exists(bundled)) return bundled.string();

    // Check project root / tools
    fs::path projectTools = exeDir / ".." / ".." / "tools" / "madmom_analyzer.exe";
    if (fs::exists(projectTools)) return fs::canonical(projectTools).string();
#endif
    return "";
}

std::string AudioAnalyzer::findAnalyzerScript() const {
#ifdef _WIN32
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();

    // Check project root / tools
    fs::path script = exeDir / ".." / ".." / "tools" / "analyze_audio.py";
    if (fs::exists(script)) return fs::canonical(script).string();

    // Also check relative to cwd
    if (fs::exists("tools/analyze_audio.py"))
        return fs::canonical("tools/analyze_audio.py").string();
#endif
    return "";
}

// ── Run subprocess with stdout pipe capture ─────────────────────────────────

AudioAnalysisResult AudioAnalyzer::runSubprocess(const std::string& audioPath) {
    AudioAnalysisResult result;

#ifdef _WIN32
    // Helper: UTF-8 narrow → UTF-16 wide. The audio path may contain non-ASCII
    // characters (e.g. CP936 Chinese filenames stored as UTF-8 in our JSON);
    // CreateProcessA would re-interpret those bytes as the system ANSI code
    // page on launch and the child process would receive a garbled path that
    // does not match any file on disk — which surfaces as Python's
    // "failed to decode file (-7)" because libsndfile/soundfile cannot find
    // (or open) a file at the corrupted path.
    auto toWide = [](const std::string& s) -> std::wstring {
        if (s.empty()) return L"";
        int wlen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        if (wlen <= 0) return L"";
        std::wstring w(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], wlen);
        return w;
    };

    // Determine command line (built as wide chars end-to-end).
    std::wstring cmdW;
    std::string exeTool = findAnalyzerExecutable();
    if (!exeTool.empty()) {
        cmdW = L"\"" + toWide(exeTool) + L"\" \"" + toWide(audioPath) + L"\"";
    } else {
        std::string script = findAnalyzerScript();
        if (!script.empty()) {
            cmdW = L"python \"" + toWide(script) + L"\" \"" + toWide(audioPath) + L"\"";
        } else {
            result.errorMessage = "Madmom analyzer not found.\n"
                "Place madmom_analyzer.exe in tools/ or ensure "
                "tools/analyze_audio.py exists with Python + madmom installed.";
            return result;
        }
    }

    std::cout << "[AudioAnalyzer] Running analyzer subprocess (wide cmdline)\n";

    // Create pipe for stdout capture
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe = nullptr, hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        result.errorMessage = "Failed to create pipe";
        return result;
    }
    // Ensure the read end is not inherited
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    // Launch process via the wide-char API so the audio path round-trips
    // unchanged to the child.
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    si.hStdError  = hWritePipe;
    si.dwFlags    = STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi{};

    std::vector<wchar_t> cmdBuf(cmdW.begin(), cmdW.end());
    cmdBuf.push_back(L'\0');

    // Force the child to use UTF-8 for its own stdio so the JSON we read back
    // through the pipe is unambiguous (avoids the analyzer printing CP936
    // bytes for any non-ASCII fields).
    std::wstring envBlockW;
    {
        wchar_t* parentEnv = GetEnvironmentStringsW();
        if (parentEnv) {
            const wchar_t* p = parentEnv;
            while (*p) { size_t n = wcslen(p); envBlockW.append(p, n + 1); p += n + 1; }
            FreeEnvironmentStringsW(parentEnv);
        }
        const wchar_t* extras[] = { L"PYTHONIOENCODING=utf-8", L"PYTHONUTF8=1" };
        for (auto* e : extras) {
            envBlockW.append(e, wcslen(e) + 1);
        }
        envBlockW.push_back(L'\0');
    }

    if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                        (LPVOID)envBlockW.data(), nullptr, &si, &pi)) {
        DWORD err = GetLastError();
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        result.errorMessage = "Failed to launch analyzer process (GetLastError="
                            + std::to_string(err) + ")";
        return result;
    }

    // Close write end in parent (so ReadFile gets EOF when child exits)
    CloseHandle(hWritePipe);

    // Read stdout
    std::string output;
    char buf[4096];
    DWORD bytesRead;
    while (ReadFile(hReadPipe, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        output += buf;
    }
    CloseHandle(hReadPipe);

    // Wait for process to finish (120s timeout)
    DWORD waitResult = WaitForSingleObject(pi.hProcess, 120000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (waitResult == WAIT_TIMEOUT) {
        result.errorMessage = "Analysis timed out (>120s)";
        return result;
    }

    if (exitCode != 0 && output.empty()) {
        result.errorMessage = "Analyzer process failed (exit code " + std::to_string(exitCode) + ")";
        return result;
    }

    std::cout << "[AudioAnalyzer] Output length: " << output.size() << " bytes\n";

    return parseJson(output);
#else
    result.errorMessage = "AudioAnalyzer only supported on Windows";
    return result;
#endif
}

// ── Simple JSON parser for our known output format ──────────────────────────
// We parse: {"status":"ok","bpm":128.0,"easy":[...],"medium":[...],"hard":[...]}
// This avoids depending on nlohmann/json for this one use case.

AudioAnalysisResult AudioAnalyzer::parseJson(const std::string& jsonStr) {
    AudioAnalysisResult result;

    // Check for "status": "error"
    if (jsonStr.find("\"error\"") != std::string::npos &&
        jsonStr.find("\"status\"") != std::string::npos) {
        // Extract error message
        auto msgPos = jsonStr.find("\"message\"");
        if (msgPos != std::string::npos) {
            auto start = jsonStr.find('"', msgPos + 9);
            if (start != std::string::npos) {
                auto end = jsonStr.find('"', start + 1);
                result.errorMessage = jsonStr.substr(start + 1, end - start - 1);
            }
        }
        if (result.errorMessage.empty())
            result.errorMessage = "Analysis failed (unknown error)";
        return result;
    }

    // Helper: extract a float array for a given key, e.g. "easy": [1.0, 2.0, ...]
    auto extractArray = [&](const std::string& key) -> std::vector<float> {
        std::vector<float> arr;
        std::string searchKey = "\"" + key + "\"";
        auto pos = jsonStr.find(searchKey);
        if (pos == std::string::npos) return arr;

        auto bracketStart = jsonStr.find('[', pos);
        if (bracketStart == std::string::npos) return arr;
        auto bracketEnd = jsonStr.find(']', bracketStart);
        if (bracketEnd == std::string::npos) return arr;

        std::string arrayStr = jsonStr.substr(bracketStart + 1, bracketEnd - bracketStart - 1);
        std::istringstream iss(arrayStr);
        std::string token;
        while (std::getline(iss, token, ',')) {
            try {
                // Trim whitespace
                size_t s = token.find_first_not_of(" \t\n\r");
                if (s != std::string::npos)
                    arr.push_back(std::stof(token.substr(s)));
            } catch (...) {}
        }
        return arr;
    };

    // Extract BPM
    auto bpmPos = jsonStr.find("\"bpm\"");
    if (bpmPos != std::string::npos) {
        auto colon = jsonStr.find(':', bpmPos);
        if (colon != std::string::npos) {
            try {
                result.bpm = std::stof(jsonStr.substr(colon + 1));
            } catch (...) { result.bpm = 120.f; }
        }
    }

    result.easyMarkers   = extractArray("easy");
    result.mediumMarkers = extractArray("medium");
    result.hardMarkers   = extractArray("hard");

    // Per-marker features (parallel arrays). Missing → zero-init feature, which
    // falls back to Tap in the inference helper.
    auto buildFeatures = [&](const std::string& level,
                             const std::vector<float>& times)
        -> std::vector<MarkerFeature>
    {
        std::vector<MarkerFeature> feats(times.size());
        auto sv = extractArray(level + "_strength");
        auto suv = extractArray(level + "_sustain");
        auto cv = extractArray(level + "_centroid");
        for (size_t i = 0; i < feats.size(); ++i) {
            if (i < sv.size())  feats[i].strength = sv[i];
            if (i < suv.size()) feats[i].sustain  = suv[i];
            if (i < cv.size())  feats[i].centroid = cv[i];
        }
        return feats;
    };
    result.easyFeatures   = buildFeatures("easy",   result.easyMarkers);
    result.mediumFeatures = buildFeatures("medium", result.mediumMarkers);
    result.hardFeatures   = buildFeatures("hard",   result.hardMarkers);

    // Parse bpm_changes: [{"time":0.0,"bpm":128.0}, ...]
    {
        std::string key = "\"bpm_changes\"";
        auto pos = jsonStr.find(key);
        if (pos != std::string::npos) {
            auto arrStart = jsonStr.find('[', pos);
            if (arrStart != std::string::npos) {
                // Find matching closing bracket (handle nested objects)
                int depth = 0;
                size_t arrEnd = arrStart;
                for (size_t i = arrStart; i < jsonStr.size(); i++) {
                    if (jsonStr[i] == '[') depth++;
                    else if (jsonStr[i] == ']') { depth--; if (depth == 0) { arrEnd = i; break; } }
                }
                std::string arrStr = jsonStr.substr(arrStart + 1, arrEnd - arrStart - 1);

                // Parse each {time, bpm} object
                size_t objPos = 0;
                while ((objPos = arrStr.find('{', objPos)) != std::string::npos) {
                    auto objEnd = arrStr.find('}', objPos);
                    if (objEnd == std::string::npos) break;
                    std::string obj = arrStr.substr(objPos, objEnd - objPos + 1);

                    BpmChange bc;
                    // Extract "time" value
                    auto tPos = obj.find("\"time\"");
                    if (tPos != std::string::npos) {
                        auto colon = obj.find(':', tPos);
                        if (colon != std::string::npos) {
                            try { bc.time = std::stof(obj.substr(colon + 1)); } catch (...) {}
                        }
                    }
                    // Extract "bpm" value
                    auto bPos = obj.find("\"bpm\"");
                    if (bPos != std::string::npos) {
                        auto colon = obj.find(':', bPos);
                        if (colon != std::string::npos) {
                            try { bc.bpm = std::stof(obj.substr(colon + 1)); } catch (...) {}
                        }
                    }
                    result.bpmChanges.push_back(bc);
                    objPos = objEnd + 1;
                }
            }
        }
    }

    result.success = !result.easyMarkers.empty() ||
                     !result.mediumMarkers.empty() ||
                     !result.hardMarkers.empty();

    if (!result.success && result.errorMessage.empty())
        result.errorMessage = "No beats detected in audio";

    std::cout << "[AudioAnalyzer] Parsed: bpm=" << result.bpm
              << " bpm_changes=" << result.bpmChanges.size()
              << " easy=" << result.easyMarkers.size()
              << " medium=" << result.mediumMarkers.size()
              << " hard=" << result.hardMarkers.size() << "\n";

    return result;
}
