// ============================================================================
// Android File I/O
// Reads assets from the APK via AAssetManager.
// Extracts files to internal storage when filesystem paths are needed (audio).
// ============================================================================
#pragma once
#include <string>
#include <vector>

struct AAssetManager;

namespace AndroidFileIO {

// Call once from android_main with the app's AAssetManager and internal data path.
void init(AAssetManager* mgr, const std::string& internalPath);

// Read an entire asset file as a string. Returns empty string on failure.
std::string readString(const std::string& assetPath);

// Read an entire asset file as raw bytes. Returns empty vector on failure.
std::vector<char> readBinary(const std::string& assetPath);

// Extract an asset to internal storage and return the filesystem path.
// Subsequent calls return the cached path without re-extracting.
// This is needed for libraries that require file paths (e.g., miniaudio).
std::string extractToInternal(const std::string& assetPath);

// Check if an asset exists.
bool exists(const std::string& assetPath);

// Get the internal storage base path.
const std::string& internalPath();

} // namespace AndroidFileIO
