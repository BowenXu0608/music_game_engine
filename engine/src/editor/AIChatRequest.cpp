#include "AIChatRequest.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>

using nlohmann::json;

// Parse "<scheme>://<host>[:port][/path]" into client origin + path prefix.
// Returns false if the URL is malformed or uses https (not supported without
// OpenSSL - see the HTTPS note in AIEditorConfig.h).
static bool parseEndpoint(const std::string& url,
                          std::string& clientOrigin,
                          std::string& pathPrefix,
                          std::string& errOut) {
    auto schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) {
        errOut = "Endpoint missing scheme (expected http://...)";
        return false;
    }
    std::string scheme = url.substr(0, schemeEnd);
    if (scheme == "https") {
        errOut = "HTTPS endpoints aren't supported in this build. Use http:// "
                 "(Ollama/local), or rebuild with OpenSSL for cloud providers.";
        return false;
    }
    if (scheme != "http") {
        errOut = "Unsupported endpoint scheme '" + scheme + "' (expected http)";
        return false;
    }
    auto rest = url.substr(schemeEnd + 3);
    auto pathSlash = rest.find('/');
    std::string hostPort;
    if (pathSlash == std::string::npos) {
        hostPort   = rest;
        pathPrefix = "";
    } else {
        hostPort   = rest.substr(0, pathSlash);
        pathPrefix = rest.substr(pathSlash);
    }
    if (!pathPrefix.empty() && pathPrefix.back() == '/')
        pathPrefix.pop_back();

    clientOrigin = scheme + "://" + hostPort;
    return true;
}

AIEditorResult runChatRequest(const AIEditorConfig& cfg,
                               const std::string& systemPrompt,
                               const std::string& userPrompt,
                               bool jsonMode) {
    AIEditorResult result;

    std::string origin, pathPrefix, err;
    if (!parseEndpoint(cfg.endpoint, origin, pathPrefix, err)) {
        result.errorMessage = err;
        return result;
    }

    httplib::Client cli(origin);
    cli.set_connection_timeout(10, 0);
    cli.set_read_timeout(cfg.timeoutSecs, 0);
    cli.set_write_timeout(10, 0);

    json body = {
        {"model", cfg.model},
        {"messages", json::array({
            {{"role", "system"}, {"content", systemPrompt}},
            {{"role", "user"},   {"content", userPrompt}}
        })},
        {"temperature", 0.2},
        {"stream", false}
    };
    if (jsonMode) {
        body["response_format"] = {{"type", "json_object"}};
    }

    // error_handler_t::replace so non-UTF-8 bytes in user prompts (CJK IMEs
    // on Windows) degrade to U+FFFD instead of throwing.
    std::string bodyStr = body.dump(-1, ' ', false,
                                    json::error_handler_t::replace);

    httplib::Headers headers = {{"Content-Type", "application/json"}};
    if (!cfg.apiKey.empty())
        headers.emplace("Authorization", "Bearer " + cfg.apiKey);

    std::string path = pathPrefix + "/chat/completions";
    std::cout << "[AIChatRequest] POST " << origin << path
              << " (model=" << cfg.model << ", jsonMode=" << jsonMode << ")\n";

    auto res = cli.Post(path.c_str(), headers, bodyStr, "application/json");
    if (!res) {
        auto errCode = res.error();
        result.errorMessage = "HTTP request failed: " +
                              httplib::to_string(errCode);
        return result;
    }
    if (res->status < 200 || res->status >= 300) {
        result.errorMessage = "Server returned HTTP " +
                              std::to_string(res->status) + ": " + res->body;
        return result;
    }

    try {
        auto parsed = json::parse(res->body);
        auto& choices = parsed.at("choices");
        if (!choices.is_array() || choices.empty()) {
            result.errorMessage = "Response had no choices";
            return result;
        }
        auto& msg = choices[0].at("message");
        result.message = msg.at("content").get<std::string>();
        result.success = true;
    } catch (const std::exception& e) {
        result.errorMessage = std::string("Couldn't parse response: ") + e.what();
    }
    return result;
}
