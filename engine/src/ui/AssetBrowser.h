#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

struct AssetList {
    std::vector<std::string> images;  // .png .jpg .jpeg
    std::vector<std::string> gifs;    // .gif
    std::vector<std::string> videos;  // .mp4 .webm
    std::vector<std::string> audios;  // .mp3 .ogg .wav .flac .aac
};

// Import files into a project's assets/ directory, routing by extension.
// Returns the number of files successfully copied.
inline int importAssetsToProject(const std::string& projectPath,
                                 const std::vector<std::string>& srcPaths) {
    if (projectPath.empty()) return 0;
    fs::path absProject = fs::absolute(fs::path(projectPath));
    int copied = 0;

    for (const auto& src : srcPaths) {
        std::string ext = fs::path(src).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        fs::path destDir;
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".gif")
            destDir = absProject / "assets" / "textures";
        else if (ext == ".mp4" || ext == ".webm")
            destDir = absProject / "assets" / "videos";
        else if (ext == ".mp3" || ext == ".ogg" || ext == ".wav" || ext == ".flac" || ext == ".aac")
            destDir = absProject / "assets" / "audio";
        else if (ext == ".json" || ext == ".chart" || ext == ".ucf")
            destDir = absProject / "assets" / "charts";
        else
            destDir = absProject / "assets";

        try {
            fs::create_directories(destDir);
            fs::path dest = destDir / fs::path(src).filename();
            fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
            ++copied;
        } catch (...) {}
    }
    return copied;
}

// Scans {projectPath}/assets/ recursively and groups files by type.
// All returned paths are relative to projectPath (e.g. "assets/textures/bg.png").
inline AssetList scanAssets(const std::string& projectPath) {
    AssetList result;
    // Use absolute path so fs::relative works correctly on Windows
    fs::path absProject = fs::absolute(fs::path(projectPath));
    fs::path assetsDir  = absProject / "assets";
    if (!fs::exists(assetsDir)) return result;

    for (const auto& entry : fs::recursive_directory_iterator(assetsDir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // Make relative to projectPath
        std::string rel = fs::relative(entry.path(), absProject).string();
        // Normalize to forward slashes
        std::replace(rel.begin(), rel.end(), '\\', '/');

        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
            result.images.push_back(rel);
        else if (ext == ".gif")
            result.gifs.push_back(rel);
        else if (ext == ".mp4" || ext == ".webm")
            result.videos.push_back(rel);
        else if (ext == ".mp3" || ext == ".ogg" || ext == ".wav" || ext == ".flac" || ext == ".aac")
            result.audios.push_back(rel);
    }
    return result;
}
