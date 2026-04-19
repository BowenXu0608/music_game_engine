#pragma once
#include <string>

// User-global config for the Editor Copilot's HTTP AI backend.
// Persisted alongside player_settings.json in the user's app-internal dir.
//
// Default values target Ollama running locally, which is free + offline +
// requires no API key. The endpoint is OpenAI-compatible (/v1/chat/completions),
// so redirecting to any other OpenAI-shaped server (Groq, LM Studio, etc.)
// just works — HTTPS-only providers are deferred until OpenSSL is linked.
struct AIEditorConfig {
    std::string endpoint    = "http://localhost:11434/v1";
    std::string model       = "qwen2.5:3b";
    std::string apiKey      = "";   // unused for Ollama; for forward-compat
    int         timeoutSecs = 180;
};

bool loadAIEditorConfig(const std::string& path, AIEditorConfig& out);
bool saveAIEditorConfig(const std::string& path, const AIEditorConfig& s);
