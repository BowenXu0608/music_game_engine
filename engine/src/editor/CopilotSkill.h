#pragma once
#include <string>

// Loads the per-mode copilot skill document (e.g. "bandori", "arcaea",
// "lanota", "cytus"). Reads the mode-specific file and concatenates it
// with the shared "_common.md" body. Returns empty string when neither
// file is present and logs once so the caller can fall back to the
// inlined prompt. Read every call (files are small, the HTTP request
// that follows dominates latency).
std::string loadCopilotSkill(const std::string& modeName);
