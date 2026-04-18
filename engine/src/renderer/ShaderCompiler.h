#pragma once
#include <string>
#include <filesystem>

// Resolve a shader file to a loadable SPIR-V path.
//
// Accepted inputs (picked by extension, case-insensitive):
//   * `.frag` or anything else — treated as GLSL source, compiled via glslc
//     to `<path>.spv`. Mtime-cached: repeated calls skip recompilation when
//     the source hasn't changed.
//   * `.spv` — taken verbatim, no glslc invocation. For shipping / mobile
//     flows where sources aren't distributed.
//   * `.hlsl` — explicitly rejected with a clear message (HLSL support is
//     not wired in yet).
//
// glslc is located via the `VULKAN_SDK` env var's `Bin/` subdirectory,
// well-known Windows install paths, and the system PATH. If glslc is needed
// but missing, `ok` is false and `errorLog` explains.

struct ShaderCompileResult {
    bool        ok = false;
    std::string spvPath;    // filled on success
    std::string errorLog;   // glslc stderr on failure (or diagnostic when ok==false)
};

// Name kept for continuity — now accepts any shader input path, not only
// `.frag`. `forceRebuild` recompiles even if the cached .spv is current
// (only meaningful for GLSL source).
ShaderCompileResult compileFragmentToSpv(const std::filesystem::path& shaderPath,
                                          bool forceRebuild = false);

// Best-effort location of glslc. Returns empty string when not found.
std::string findGlslcPath();
