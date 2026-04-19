#include "ShaderGenClient.h"
#include "renderer/ShaderCompiler.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>

using nlohmann::json;
namespace fs = std::filesystem;

// ── System prompt ──────────────────────────────────────────────────────────
// Locks the reply shape to {"shader": "<glsl source>"} and enumerates the
// exact layout bindings the batcher's custom pipeline expects. Any deviation
// and glslc will fail; the retry loop then surfaces the glslc errors back to
// the model.
static const char* kShaderGenSystemPrompt =
    "You adapt a Vulkan/GLSL 450 fragment-shader TEMPLATE for a 2D music-game engine.\n"
    "Return JSON ONLY of the form {\"shader\": \"<full glsl source>\"} - no prose, no fences.\n"
    "\n"
    "START FROM THIS TEMPLATE. Keep every layout() binding and every declaration above main()\n"
    "EXACTLY as shown. Modify ONLY the body of main() to implement the user's request.\n"
    "Do NOT add extra uniforms / samplers / in / out locations. Do NOT add helper functions.\n"
    "\n"
    "=== TEMPLATE START ===\n"
    "#version 450\n"
    "layout(set = 0, binding = 0) uniform FrameUBO { mat4 viewProj; float time; } ubo;\n"
    "layout(set = 1, binding = 0) uniform sampler2D texSampler;\n"
    "layout(push_constant) uniform PC {\n"
    "    mat4  model;\n"
    "    vec4  tint;\n"
    "    vec4  uvTransform;\n"
    "    vec4  params;\n"
    "    uint  kind;\n"
    "    uint  _pad[3];\n"
    "} pc;\n"
    "layout(location = 0) in vec2 fragUV;\n"
    "layout(location = 1) in vec4 fragColor;\n"
    "layout(location = 0) out vec4 outColor;\n"
    "\n"
    "void main() {\n"
    "    // Oscillator in [0,1]. Change speed/shape as the user asks.\n"
    "    float pulse = 0.5 + 0.5 * sin(ubo.time * 2.0);\n"
    "    // Named color goes HERE as a literal vec3. Replace with the user's color.\n"
    "    vec3 baseColor = vec3(0.6, 0.2, 1.0); // purple placeholder\n"
    "    // Optional: sample the texture. Keep this line even if you don't use texCol.\n"
    "    vec4 texCol = texture(texSampler, fragUV);\n"
    "    // Final color. Must multiply by pc.tint.rgb so the tint slider works.\n"
    "    vec3 col = baseColor * pc.tint.rgb * pulse;\n"
    "    float alpha = 0.8 + 0.2 * sin(ubo.time * 1.5 + fragUV.y * 6.28);\n"
    "    outColor = vec4(col, clamp(alpha, 0.1, 1.0));\n"
    "    if (outColor.a < 0.01) discard;\n"
    "}\n"
    "=== TEMPLATE END ===\n"
    "\n"
    "HARD RULES (violating any of these is a bug):\n"
    "- To access time, ALWAYS write `ubo.time`. Bare `time` is undeclared.\n"
    "- To access viewProj, ALWAYS write `ubo.viewProj`. Bare `viewProj` is undeclared.\n"
    "- When the user names a color (purple, red, cyan, gold, green, ...), put it in `baseColor`\n"
    "  as an explicit vec3(r,g,b) literal. Do not rely on pc.tint alone - pc.tint defaults to\n"
    "  white and only scales the output.\n"
    "- Any sin()/cos() used as a brightness or alpha multiplier must be wrapped into a positive\n"
    "  range: `0.5 + 0.5 * sin(...)` is correct; `cos(...) + 0.5` is WRONG (goes negative,\n"
    "  clamps to black, causes flicker).\n"
    "- outColor.a must stay in [0.1, 1.0] on average. Never write a shader that goes permanently\n"
    "  transparent or permanently black.\n"
    "- Type-check every multiplication. vec4 * vec3 is ILLEGAL (use .rgb). vec2 * vec4 is\n"
    "  ILLEGAL (use matching dimensions). When in doubt, decompose into scalar multiplies.\n"
    "- Do NOT read fragColor unless the user explicitly asks.\n"
    "- Do NOT use smoothstep with time-varying edges.\n"
    "- Write only one function: `void main()`. No helpers. Discard goes INSIDE main().\n"
    "- Keep the code under 60 lines.\n";

// Extract the shader field from a chat reply. Tolerates prose around the
// JSON (small models sometimes break JSON mode) by locating the outermost
// {...} and parsing that slice.
static std::string extractShader(const std::string& msg) {
    auto lbr = msg.find('{');
    auto rbr = msg.rfind('}');
    if (lbr == std::string::npos || rbr == std::string::npos || rbr <= lbr)
        return {};
    std::string slice = msg.substr(lbr, rbr - lbr + 1);
    try {
        auto j = json::parse(slice);
        if (j.contains("shader") && j["shader"].is_string())
            return j["shader"].get<std::string>();
    } catch (...) {}
    return {};
}

ShaderGenClient::~ShaderGenClient() { cancel(); }

void ShaderGenClient::startGeneration(const AIEditorConfig& cfg,
                                       const std::string& userPrompt,
                                       const std::string& outFragPath,
                                       int maxAttempts) {
    if (m_running.load()) return;

    m_finished.store(false);
    m_running.store(true);
    if (m_thread.joinable()) m_thread.join();

    AIEditorConfig cfgCopy = cfg;
    std::string up  = userPrompt;
    std::string out = outFragPath;
    int N = std::max(1, maxAttempts);

    {
        std::lock_guard<std::mutex> lock(m_statusMutex);
        m_liveStatus = {};
        m_liveStatus.maxAttempts = N;
    }

    m_thread = std::thread([this, cfgCopy, up, out, N]() {
        ShaderGenResult r;
        try {
            r = runLoop(cfgCopy, up, out, N);
        } catch (const std::exception& e) {
            r.errorMessage = std::string("ShaderGen worker exception: ") + e.what();
            std::cerr << "[ShaderGenClient] " << r.errorMessage << "\n";
        } catch (...) {
            r.errorMessage = "ShaderGen worker threw an unknown exception";
            std::cerr << "[ShaderGenClient] " << r.errorMessage << "\n";
        }
        {
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_result = std::move(r);
        }
        m_finished.store(true);
        m_running.store(false);
    });
}

void ShaderGenClient::pollCompletion() {
    if (!m_finished.load()) return;
    m_finished.store(false);
    ShaderGenResult r;
    {
        std::lock_guard<std::mutex> lock(m_resultMutex);
        r = std::move(m_result);
    }
    if (m_callback) m_callback(std::move(r));
    if (m_thread.joinable()) m_thread.join();
}

void ShaderGenClient::cancel() {
    m_running.store(false);
    m_finished.store(false);
    if (m_thread.joinable()) m_thread.join();
}

ShaderGenResult ShaderGenClient::runLoop(const AIEditorConfig& cfg,
                                          const std::string& userPrompt,
                                          const std::string& outFragPath,
                                          int maxAttempts) {
    ShaderGenResult out;
    std::string lastErrors;
    std::string lastFrag;

    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_liveStatus.attempt = attempt;
            m_liveStatus.phase   = "sending";
        }

        std::string up = userPrompt;
        if (!lastErrors.empty()) {
            up += "\n\nThe previous shader FAILED to compile. Read the error carefully,\n"
                  "identify the exact identifier or line causing it, and fix ONLY that.\n"
                  "Common fixes:\n"
                  "  - `time`            -> `ubo.time`\n"
                  "  - `viewProj`        -> `ubo.viewProj`\n"
                  "  - bare `tint`       -> `pc.tint`\n"
                  "  - `color`/`uv` etc. -> declare as local variables first\n"
                  "Do NOT rewrite the whole approach - apply minimal edits and resubmit.\n"
                  "Previous shader:\n```glsl\n" + lastFrag + "\n```\n"
                  "Compiler errors:\n" + lastErrors + "\n"
                  "Return the corrected FULL shader in the same JSON envelope.";
        }

        AIEditorResult res = runChatRequest(cfg, kShaderGenSystemPrompt, up,
                                             /*jsonMode=*/true);
        out.attempts = attempt;
        if (!res.success) {
            out.errorMessage  = "Attempt " + std::to_string(attempt) + ": " +
                                res.errorMessage;
            out.attemptsLog  += out.errorMessage + "\n";
            return out;
        }

        std::string frag = extractShader(res.message);
        if (frag.empty()) {
            out.errorMessage = "Attempt " + std::to_string(attempt) +
                               ": couldn't extract {\"shader\":...} from response";
            out.attemptsLog += out.errorMessage + "\n---raw reply---\n" +
                               res.message + "\n";
            return out;
        }
        lastFrag = frag;

        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_liveStatus.phase = "writing";
        }
        std::error_code ec;
        fs::create_directories(fs::path(outFragPath).parent_path(), ec);
        {
            std::ofstream f(outFragPath, std::ios::binary);
            if (!f) {
                out.errorMessage = "Couldn't open " + outFragPath + " for writing";
                return out;
            }
            f << frag;
        }

        {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_liveStatus.phase = "compiling";
        }
        auto cr = compileFragmentToSpv(outFragPath, /*forceRebuild=*/true);
        if (cr.ok) {
            out.success      = true;
            out.spvPath      = cr.spvPath;
            out.fragSource   = frag;
            out.attemptsLog += "Attempt " + std::to_string(attempt) + ": OK\n";
            return out;
        }

        out.attemptsLog += "Attempt " + std::to_string(attempt) +
                           ": glslc failed:\n" + cr.errorLog + "\n";
        lastErrors = cr.errorLog;

        if (attempt < maxAttempts) {
            std::lock_guard<std::mutex> lock(m_statusMutex);
            m_liveStatus.phase = "retrying";
        }
    }

    out.errorMessage = "Exhausted " + std::to_string(maxAttempts) +
                       " attempts. Last glslc errors:\n" + lastErrors;
    return out;
}
