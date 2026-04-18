#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

// Convert a UTF-8 std::string to fs::path correctly on Windows (C++20).
inline fs::path u8ToPath(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

struct AssetList {
    std::vector<std::string> images;     // .png .jpg .jpeg
    std::vector<std::string> gifs;       // .gif
    std::vector<std::string> videos;     // .mp4 .webm
    std::vector<std::string> audios;     // .mp3 .ogg .wav .flac .aac
    std::vector<std::string> materials;  // .mat — MaterialAsset JSON files
};

inline int importAssetsToProject(const std::string& projectPath,
                                 const std::vector<std::string>& srcPaths) {
    if (projectPath.empty()) return 0;
    int copied = 0;
    try {
        fs::path absProject = fs::absolute(fs::path(projectPath));
        for (const auto& src : srcPaths) {
            try {
                fs::path srcPath = u8ToPath(src);
                std::string ext = srcPath.extension().string();
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

                fs::create_directories(destDir);
                fs::path dest = destDir / srcPath.filename();
                fs::copy_file(srcPath, dest, fs::copy_options::overwrite_existing);
                ++copied;
            } catch (const std::exception& e) {
                std::cout << "[AssetBrowser] Copy failed: " << e.what() << "\n";
            }
        }
    } catch (const std::exception& e) {
        std::cout << "[AssetBrowser] Import error: " << e.what() << "\n";
    }
    return copied;
}

inline AssetList scanAssets(const std::string& projectPath) {
    AssetList result;
    try {
        fs::path absProject = fs::absolute(fs::path(projectPath));
        fs::path assetsDir  = absProject / "assets";
        if (!fs::exists(assetsDir)) return result;

        for (const auto& entry : fs::recursive_directory_iterator(assetsDir)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

            std::string rel = fs::relative(entry.path(), absProject).string();
            std::replace(rel.begin(), rel.end(), '\\', '/');

            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
                result.images.push_back(rel);
            else if (ext == ".gif")
                result.gifs.push_back(rel);
            else if (ext == ".mp4" || ext == ".webm")
                result.videos.push_back(rel);
            else if (ext == ".mp3" || ext == ".ogg" || ext == ".wav" || ext == ".flac" || ext == ".aac")
                result.audios.push_back(rel);
            else if (ext == ".mat")
                result.materials.push_back(rel);
        }
    } catch (const std::exception& e) {
        std::cout << "[AssetBrowser] Scan error: " << e.what() << "\n";
    }
    return result;
}
