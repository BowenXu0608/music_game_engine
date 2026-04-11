// ============================================================================
// Android File I/O Implementation
// ============================================================================
#include "AndroidFileIO.h"
#include <android/asset_manager.h>
#include <android/log.h>
#include <fstream>
#include <unordered_set>
#include <sys/stat.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "MusicGame", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "MusicGame", __VA_ARGS__)

static AAssetManager* s_assetMgr = nullptr;
static std::string    s_internalPath;
static std::unordered_set<std::string> s_extracted;  // already-extracted assets

namespace AndroidFileIO {

void init(AAssetManager* mgr, const std::string& internalPath) {
    s_assetMgr    = mgr;
    s_internalPath = internalPath;
    if (!s_internalPath.empty() && s_internalPath.back() != '/')
        s_internalPath += '/';
    LOGI("AndroidFileIO initialized: %s", s_internalPath.c_str());
}

std::string readString(const std::string& assetPath) {
    if (!s_assetMgr) return "";
    AAsset* asset = AAssetManager_open(s_assetMgr, assetPath.c_str(), AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("Failed to open asset: %s", assetPath.c_str());
        return "";
    }
    off_t len = AAsset_getLength(asset);
    std::string data(len, '\0');
    AAsset_read(asset, &data[0], len);
    AAsset_close(asset);
    return data;
}

std::vector<char> readBinary(const std::string& assetPath) {
    if (!s_assetMgr) return {};
    AAsset* asset = AAssetManager_open(s_assetMgr, assetPath.c_str(), AASSET_MODE_BUFFER);
    if (!asset) {
        LOGE("Failed to open asset: %s", assetPath.c_str());
        return {};
    }
    off_t len = AAsset_getLength(asset);
    std::vector<char> data(len);
    AAsset_read(asset, data.data(), len);
    AAsset_close(asset);
    return data;
}

// Recursively create directories for a file path
static void ensureParentDir(const std::string& path) {
    size_t pos = 0;
    while ((pos = path.find('/', pos + 1)) != std::string::npos) {
        std::string dir = path.substr(0, pos);
        mkdir(dir.c_str(), 0755);
    }
}

std::string extractToInternal(const std::string& assetPath) {
    std::string outPath = s_internalPath + assetPath;

    // Already extracted this session?
    if (s_extracted.count(assetPath))
        return outPath;

    // Already exists on disk from a previous run?
    struct stat st;
    if (stat(outPath.c_str(), &st) == 0) {
        s_extracted.insert(assetPath);
        return outPath;
    }

    // Extract from APK
    auto data = readBinary(assetPath);
    if (data.empty()) {
        LOGE("Cannot extract asset: %s", assetPath.c_str());
        return "";
    }

    ensureParentDir(outPath);
    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) {
        LOGE("Cannot write to: %s", outPath.c_str());
        return "";
    }
    out.write(data.data(), data.size());
    out.close();

    s_extracted.insert(assetPath);
    LOGI("Extracted: %s -> %s", assetPath.c_str(), outPath.c_str());
    return outPath;
}

bool exists(const std::string& assetPath) {
    if (!s_assetMgr) return false;
    AAsset* asset = AAssetManager_open(s_assetMgr, assetPath.c_str(), AASSET_MODE_UNKNOWN);
    if (asset) {
        AAsset_close(asset);
        return true;
    }
    return false;
}

const std::string& internalPath() {
    return s_internalPath;
}

} // namespace AndroidFileIO
