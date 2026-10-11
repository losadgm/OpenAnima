#include "OpenAnima/Provider/RestProvider.h"

#include <cassert>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#include "OpenAnima/Errors.h"

namespace OpenAnima::Provider {

namespace {

using OpenAnima::Core::ChatMessage;
using OpenAnima::Core::Role;
using OpenAnima::Http::HttpRequest;
using OpenAnima::Http::HttpResponse;

/// Longest fragment of a foreign payload embedded in an error message.
constexpr std::size_t BodySnippetLength = 200;

const char* roleName(Role role) {
    switch (role) {
    case Role::System:
        return "system";
    case Role::User:
        return "user";
    case Role::Assistant:
        return "assistant";
    case Role::Tool:
        return "tool";
    }
    return "user";
}

/// Flattens a payload into one short line so it stays readable inside an error message.
std::string snippet(const std::string& payload) {
    std::string result;
    result.reserve(BodySnippetLength + 3);
    for (const char character : payload) {
        if (result.size() >= BodySnippetLength) {
            result += "...";
            break;
        }
        const bool isWhitespace = character == '\n' || character == '\r' || character == '\t';
        result += isWhitespace ? ' ' : character;
    }
    return result;
}

/// Marshals the conversation into the chat-completions request body.
std::string buildRequestBody(const ProviderConfig& config,
                             const std::vector<ChatMessage>& messages) {
    nlohmann::json conversation = nlohmann::json::array();
    for (const ChatMessage& message : messages) {
        conversation.push_back({{"role", roleName(message.role)}, {"content", message.content}});
    }

    const nlohmann::json body = {
        {"model", config.model},
        {"messages", conversation},
        {"temperature", config.temperature},
        {"max_tokens", config.maxTokens},
        {"stream", false},
    };
    return body.dump();
}

/// Extracts the assistant text from a chat-completions payload.
/// @throws ProviderError when the payload carries an error object.
/// @throws ParseError when the payload is not JSON or lacks the expected fields.
std::string parseAssistantText(const std::string& payload) {
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(payload);
    } catch (const nlohmann::json::exception& error) {
        throw ParseError("provider answered with malformed JSON: " + std::string(error.what()));
    }

    // Some gateways report failures as HTTP 200 with an error object in the body.
    if (parsed.contains("error")) {
        throw ProviderError("provider reported an error: " + snippet(parsed["error"].dump()), 0);
    }

    if (!parsed.contains("choices") || !parsed["choices"].is_array() || parsed["choices"].empty()) {
        throw ParseError("provider answer has no non-empty 'choices' array: " + snippet(payload));
    }

    const nlohmann::json& choice = parsed["choices"][0];
    const bool hasContent = choice.contains("message") && choice["message"].contains("content") &&
                            choice["message"]["content"].is_string();
    if (!hasContent) {
        throw ParseError("provider answer has no text in 'choices[0].message.content': " +
                         snippet(payload));
    }

    return choice["message"]["content"].get<std::string>();
}

}  // namespace

RestProvider::RestProvider(ProviderConfig config,
                           std::shared_ptr<OpenAnima::Http::HttpTransport> transport)
    : m_config(std::move(config)), m_transport(std::move(transport)) {
    assert(!m_config.baseUrl.empty() && "RestProvider requires a base URL");
    assert(!m_config.model.empty() && "RestProvider requires a model");
    assert(m_transport != nullptr && "RestProvider requires a transport");
}

std::string RestProvider::complete(const std::vector<ChatMessage>& messages) {
    assert(!messages.empty() && "complete() requires at least one message");

    HttpRequest request;
    request.url = m_config.chatCompletionsUrl();
    request.body = buildRequestBody(m_config, messages);
    request.timeoutSeconds = m_config.timeoutSeconds;
    request.headers.emplace_back("Content-Type", "application/json");
    request.headers.emplace_back("Accept", "application/json");
    if (!m_config.apiKey.empty()) {
        request.headers.emplace_back("Authorization", "Bearer " + m_config.apiKey);
    }

    const HttpResponse response = m_transport->post(request);
    if (response.statusCode < 200 || response.statusCode >= 300) {
        throw ProviderError("provider answered with HTTP " + std::to_string(response.statusCode) +
                                ": " + snippet(response.body),
                            response.statusCode);
    }
    return parseAssistantText(response.body);
}

}  // namespace OpenAnima::Provider
