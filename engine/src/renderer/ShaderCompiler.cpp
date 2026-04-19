#include "ShaderCompiler.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <system_error>

#ifdef _WIN32
  #define POPEN  _popen
  #define PCLOSE _pclose
#else
  #define POPEN  popen
  #define PCLOSE pclose
#endif

namespace fs = std::filesystem;

namespace {

// Cached across calls so we don't rediscover glslc on every compile.
std::string g_glslcCache;
bool        g_glslcCacheFilled = false;

bool fileExists(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) && fs::is_regular_file(p, ec);
}

std::string quoteForShell(const std::string& s) {
    // On Windows cmd.exe: wrap in double-quotes, escape embedded quotes.
    // On POSIX sh: same treatment works.
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        if (c == '"') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

} // namespace

std::string findGlslcPath() {
    if (g_glslcCacheFilled) return g_glslcCache;
    g_glslcCacheFilled = true;

    // 1) $VULKAN_SDK/Bin/glslc[.exe]
    if (const char* sdk = std::getenv("VULKAN_SDK")) {
        fs::path candidate = fs::path(sdk) / "Bin" /
#ifdef _WIN32
            "glslc.exe";
#else
            "glslc";
#endif
        if (fileExists(candidate)) {
            g_glslcCache = candidate.string();
            return g_glslcCache;
        }
    }

    // 2) A few well-known Windows install paths — mirrors CMakeLists.txt.
#ifdef _WIN32
    for (const char* p : {
        "C:/VulkanSDK/1.4.335.0/Bin/glslc.exe",
        "C:/VulkanSDK/1.3.296.0/Bin/glslc.exe",
    }) {
        if (fileExists(p)) { g_glslcCache = p; return g_glslcCache; }
    }
#endif

    // 3) PATH lookup: `where` on Windows, `which` on POSIX.
#ifdef _WIN32
    const char* probeCmd = "where glslc 2>nul";
#else
    const char* probeCmd = "command -v glslc 2>/dev/null";
#endif
    FILE* p = POPEN(probeCmd, "r");
    if (p) {
        char buf[512] = {};
        if (std::fgets(buf, sizeof(buf), p)) {
            std::string line = buf;
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
                line.pop_back();
            if (!line.empty() && fileExists(line))
                g_glslcCache = line;
        }
        PCLOSE(p);
    }
    return g_glslcCache;
}

ShaderCompileResult compileFragmentToSpv(const fs::path& shaderPath, bool forceRebuild) {
    ShaderCompileResult r;
    if (!fileExists(shaderPath)) {
        r.errorLog = "Shader not found: " + shaderPath.string();
        return r;
    }

    // Lowercase the extension so case variations (.SPV, .Frag) hit the same
    // branch as their canonical form.
    std::string ext = shaderPath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Pre-compiled SPIR-V: load as-is, skip glslc entirely. This is the path
    // for shipping builds where source shaders aren't distributed.
    if (ext == ".spv") {
        r.ok      = true;
        r.spvPath = shaderPath.string();
        return r;
    }

    // HLSL: explicit rejection with a clear hint. Cheap to add real HLSL
    // support later — glslc handles it with `-x hlsl` — but until a user
    // actually needs it, a helpful error beats silent misbehaviour.
    if (ext == ".hlsl") {
        r.errorLog = "HLSL not supported yet — please convert to GLSL (.frag).";
        return r;
    }

    // Anything else: treat as GLSL source. Compile to <path>.spv with cache.
    fs::path spvPath = shaderPath;
    spvPath += ".spv";

    // mtime cache: skip compile when .spv exists and is newer than the source.
    if (!forceRebuild && fileExists(spvPath)) {
        std::error_code ec;
        auto srcTime = fs::last_write_time(shaderPath, ec);
        auto spvTime = fs::last_write_time(spvPath, ec);
        if (!ec && spvTime >= srcTime) {
            r.ok      = true;
            r.spvPath = spvPath.string();
            return r;
        }
    }

    std::string glslc = findGlslcPath();
    if (glslc.empty()) {
        r.errorLog = "glslc not found — set VULKAN_SDK or add glslc to PATH";
        return r;
    }

    // glslc <src> -o <dst> 2>&1  - merge stderr into stdout for capture.
    std::ostringstream cmd;
    cmd << quoteForShell(glslc)
        << " " << quoteForShell(shaderPath.string())
        << " -o " << quoteForShell(spvPath.string())
        << " 2>&1";
    std::string cmdStr = cmd.str();

#ifdef _WIN32
    // cmd.exe /c pitfall: when the command string starts with `"..."` and
    // contains more than one pair of quotes, cmd.exe strips the leading/
    // trailing quote, which mangles the glslc path into `glslc.exe"` and
    // Windows returns ERROR_PATH_NOT_FOUND. Wrapping the whole thing in an
    // extra outer quote pair feeds cmd.exe the quotes it wants to strip and
    // leaves our inner quoting intact.
    cmdStr = "\"" + cmdStr + "\"";
#endif

    FILE* pipe = POPEN(cmdStr.c_str(), "r");
    if (!pipe) {
        r.errorLog = "Failed to launch glslc";
        return r;
    }
    std::ostringstream out;
    char buf[1024];
    while (std::fgets(buf, sizeof(buf), pipe)) out << buf;
    int code = PCLOSE(pipe);

    if (code == 0) {
        r.ok       = true;
        r.spvPath  = spvPath.string();
        r.errorLog = out.str();     // glslc sometimes prints warnings here
    } else {
        r.errorLog = out.str();
        if (r.errorLog.empty())
            r.errorLog = "glslc exited with code " + std::to_string(code);
    }
    return r;
}
