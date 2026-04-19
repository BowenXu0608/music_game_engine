#pragma once
#include "AIEditorConfig.h"

#include <string>

// Synchronous OpenAI-compatible /chat/completions POST. Extracted from the
// first copilot client so multiple async consumers (Editor Copilot, AI shader
// generator) share one HTTP/JSON path instead of drifting apart.
//
// Returns assistant message content on success (message field), or a
// human-readable reason on failure (errorMessage field). Temperature is fixed
// low (0.2) so structured responses stay deterministic.
//
// jsonMode == true  : sets response_format.json_object so backends constrain
//                     the decoder to valid JSON (Ollama / OpenAI honor this).
// jsonMode == false : free-form replies (for clients that prefer extracting
//                     code fences from prose).
struct AIEditorResult {
    bool        success = false;
    std::string message;       // assistant message content on success
    std::string errorMessage;  // human-readable reason on failure
};

AIEditorResult runChatRequest(const AIEditorConfig& cfg,
                               const std::string& systemPrompt,
                               const std::string& userPrompt,
                               bool jsonMode = true);
