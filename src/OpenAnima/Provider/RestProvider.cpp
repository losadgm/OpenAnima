#include "OpenAnima/Provider/RestProvider.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "OpenAnima/Core/Completion.h"
#include "OpenAnima/Errors.h"
#include "OpenAnima/Provider/SseReader.h"

namespace OpenAnima::Provider {

namespace {

using OpenAnima::Core::ChatMessage;
using OpenAnima::Core::Completion;
using OpenAnima::Core::FinishReason;
using OpenAnima::Core::ResponseFormat;
using OpenAnima::Core::Role;
using OpenAnima::Core::TokenUsage;
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
/// @param config Settings of the call.
/// @param messages Conversation to send.
/// @param streamed Whether the answer is asked for as a stream of chunks.
/// @param format Shape the answer is asked for in.
std::string buildRequestBody(const RestProviderConfig& config,
                             const std::vector<ChatMessage>& messages, bool streamed,
                             const ResponseFormat& format) {
    nlohmann::json conversation = nlohmann::json::array();
    for (const ChatMessage& message : messages) {
        conversation.push_back({{"role", roleName(message.role)}, {"content", message.content}});
    }

    nlohmann::json body = {
        {"model", config.model},
        {"messages", conversation},
        {"temperature", config.temperature},
        {"max_tokens", config.maxTokens},
        {"stream", streamed},
    };
    if (streamed) {
        // Without this the answer comes back without its token usage, which the other
        // mode reports. A server that does not know the field ignores it, and then
        // reports none, which the contract allows.
        body["stream_options"] = {{"include_usage", true}};
    }
    switch (format.kind) {
    case ResponseFormat::Kind::Text:
        // Nothing to ask for: free text is what the contract answers with by default.
        break;
    case ResponseFormat::Kind::JsonObject:
        body["response_format"] = {{"type", "json_object"}};
        break;
    case ResponseFormat::Kind::JsonSchema:
        // The name is an identifier the contract asks for, and the schema travels as it
        // was given: how strict it is is the schema's business, not this one's.
        body["response_format"] = {{"type", "json_schema"},
                                   {"json_schema", {{"name", "reply"}, {"schema", format.schema}}}};
        break;
    }
    return body.dump();
}

/// Maps the finish_reason string of the contract onto the modelled values.
FinishReason toFinishReason(const std::string& name) {
    if (name == "stop") {
        return FinishReason::Stop;
    }
    if (name == "length") {
        return FinishReason::Length;
    }
    if (name == "tool_calls") {
        return FinishReason::ToolCalls;
    }
    if (name == "content_filter") {
        return FinishReason::ContentFilter;
    }
    return FinishReason::Unknown;
}

/// @return The count stored at name, or 0 when it is absent or not a number.
std::int32_t readTokenCount(const nlohmann::json& usage, const char* name) {
    if (!usage.contains(name) || !usage[name].is_number()) {
        return 0;
    }
    return static_cast<std::int32_t>(usage[name].get<double>());
}

/// @return The reported token usage, empty when the payload carries none.
std::optional<TokenUsage> parseUsage(const nlohmann::json& payload) {
    if (!payload.contains("usage") || !payload["usage"].is_object()) {
        return std::nullopt;
    }

    const nlohmann::json& usage = payload["usage"];
    TokenUsage tokens;
    tokens.promptTokens = readTokenCount(usage, "prompt_tokens");
    tokens.completionTokens = readTokenCount(usage, "completion_tokens");
    tokens.totalTokens = readTokenCount(usage, "total_tokens");
    return tokens;
}

/// Reads the answer as the JSON that was asked for, when one was asked for.
///
/// A turn the caller cancelled is left alone: it never had the chance to be whole, and
/// what it produced is kept as it is, the way the text is.
/// @param completion Answer to read, its parsed form set in place.
/// @param format Shape the call asked for.
/// @throws ParseError when a JSON answer was asked for and the text is not JSON.
void readJsonAnswer(Completion& completion, const ResponseFormat& format) {
    if (format.kind == ResponseFormat::Kind::Text ||
        completion.finishReason == FinishReason::Cancelled) {
        return;
    }
    try {
        completion.json = nlohmann::json::parse(completion.text);
    } catch (const nlohmann::json::exception& error) {
        throw ParseError("the answer was asked for as JSON and is not: " +
                         std::string(error.what()) + ": " + snippet(completion.text));
    }
}

/// Extracts the assistant answer and its metadata from a chat-completions payload.
/// @throws ProviderError when the payload carries an error object.
/// @throws ParseError when the payload is not JSON or lacks the expected fields.
Completion parseCompletion(const std::string& payload) {
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

    Completion completion;
    completion.text = choice["message"]["content"].get<std::string>();
    if (parsed.contains("model") && parsed["model"].is_string()) {
        completion.model = parsed["model"].get<std::string>();
    }
    // A streaming chunk can carry a null finish_reason, so the value is checked first.
    if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
        completion.finishReason = toFinishReason(choice["finish_reason"].get<std::string>());
    }
    completion.usage = parseUsage(parsed);
    return completion;
}

/// The sentinel a chat-completions stream ends with. It is not part of the event
/// format: it is how this contract says the answer is complete.
constexpr std::string_view DoneSentinel = "[DONE]";

/// @return Whether the payload is the sentinel, allowing for stray blanks around it.
bool isDoneSentinel(std::string_view payload) {
    while (!payload.empty() && (payload.back() == ' ' || payload.back() == '\t')) {
        payload.remove_suffix(1);
    }
    return payload == DoneSentinel;
}

/// Reads one chunk of a streamed answer and folds it into the answer being assembled.
///
/// A chunk can arrive with an empty delta, with a delta carrying no text, with the
/// finish reason on its own, or carrying nothing but the token usage. All four are
/// normal and none of them is a failure.
/// @param payload JSON of one chunk.
/// @param statusCode Status of the exchange, carried by any error the chunk reports.
/// @param completion Answer being assembled.
/// @param onDelta Called with the text of the chunk, when it carries any.
/// @throws ProviderError when the chunk reports an error.
/// @throws ParseError when the chunk is not JSON, or carries no choices at all.
void applyChunk(const std::string& payload, long statusCode, Completion& completion,
                const DeltaSink& onDelta) {
    nlohmann::json parsed;
    try {
        parsed = nlohmann::json::parse(payload);
    } catch (const nlohmann::json::exception& error) {
        throw ParseError("provider streamed a chunk that is not JSON: " +
                         std::string(error.what()));
    }

    if (parsed.contains("error")) {
        throw ProviderError("provider reported an error: " + snippet(parsed["error"].dump()),
                            statusCode);
    }
    if (parsed.contains("model") && parsed["model"].is_string()) {
        completion.model = parsed["model"].get<std::string>();
    }
    // Only a chunk that reports usage may overwrite it: the rest carry a null.
    if (const std::optional<TokenUsage> usage = parseUsage(parsed); usage.has_value()) {
        completion.usage = usage;
    }

    if (!parsed.contains("choices") || !parsed["choices"].is_array()) {
        throw ParseError("provider streamed a chunk with no 'choices' array: " + snippet(payload));
    }
    if (parsed["choices"].empty()) {
        // The chunk that carries only the token usage reports an empty array.
        return;
    }

    const nlohmann::json& choice = parsed["choices"][0];
    if (choice.contains("finish_reason") && choice["finish_reason"].is_string()) {
        completion.finishReason = toFinishReason(choice["finish_reason"].get<std::string>());
    }

    const bool hasText = choice.contains("delta") && choice["delta"].contains("content") &&
                         choice["delta"]["content"].is_string();
    if (!hasText) {
        return;
    }
    const std::string text = choice["delta"]["content"].get<std::string>();
    if (text.empty()) {
        // The first chunk of a stream announces the role with no text in it.
        return;
    }
    completion.text += text;
    if (onDelta) {
        onDelta(text);
    }
}

}  // namespace

RestProvider::RestProvider(RestProviderConfig config,
                           std::shared_ptr<OpenAnima::Http::HttpTransport> transport)
    : m_config(std::move(config)), m_transport(std::move(transport)) {
    assert(!m_config.baseUrl.empty() && "RestProvider requires a base URL");
    assert(!m_config.model.empty() && "RestProvider requires a model");
    assert(m_transport != nullptr && "RestProvider requires a transport");
}

Completion RestProvider::complete(const std::vector<ChatMessage>& messages, const Call& call) {
    assert(!messages.empty() && "complete() requires at least one message");

    // A caller that wants the pieces gets a streamed answer; one that wants it whole
    // gets a single response, which is also the mode every server reports usage in.
    const bool streamed = static_cast<bool>(call.onDelta);

    HttpRequest request;
    request.url = m_config.chatCompletionsUrl();
    request.body = buildRequestBody(m_config, messages, streamed, call.format);
    request.timeoutSeconds = m_config.timeoutSeconds;
    // The transport asks this on its own timer, which is what lets a caller abandon a
    // stream that went quiet instead of waiting for the silence to time out.
    request.keepGoing = call.keepGoing;
    request.headers.emplace_back("Content-Type", "application/json");
    request.headers.emplace_back("Accept", streamed ? "text/event-stream" : "application/json");
    if (!m_config.apiKey.empty()) {
        request.headers.emplace_back("Authorization", "Bearer " + m_config.apiKey);
    }

    if (streamed) {
        return readStream(request, call);
    }

    const HttpResponse response = m_transport->post(request);
    if (response.statusCode < 200 || response.statusCode >= 300) {
        throw ProviderError("provider answered with HTTP " + std::to_string(response.statusCode) +
                                ": " + snippet(response.body),
                            response.statusCode);
    }
    Completion completion = parseCompletion(response.body);
    readJsonAnswer(completion, call.format);
    return completion;
}

Completion RestProvider::readStream(const HttpRequest& request, const Call& call) {
    Completion completion;
    long statusCode = 0;
    std::string errorBody;
    SseReader reader;

    Http::ResponseObserver observer;
    observer.onStatus = [&statusCode](long status) { statusCode = status; };
    observer.onBody = [&](std::string_view piece) -> bool {
        if (statusCode < 200 || statusCode >= 300) {
            // The body of a rejected call is not a stream: keep it whole so the error
            // can say what the server objected to.
            errorBody.append(piece);
            return true;
        }
        return reader.feed(piece, [&](std::string_view payload) {
            if (isDoneSentinel(payload)) {
                // Nothing follows the sentinel, so the exchange can end here instead of
                // waiting for the server to close a connection it is done with.
                return false;
            }
            applyChunk(std::string(payload), statusCode, completion, call.onDelta);
            return true;
        });
    };

    m_transport->post(request, observer);

    if (statusCode < 200 || statusCode >= 300) {
        throw ProviderError("provider answered with HTTP " + std::to_string(statusCode) + ": " +
                                snippet(errorBody),
                            statusCode);
    }
    // A caller that stopped the call gets what had been produced by then, marked for what
    // it is. An answer the server had already finished is left alone: the check can turn
    // false a moment after the last token arrives.
    if (completion.finishReason == FinishReason::Unknown && call.keepGoing && !call.keepGoing()) {
        completion.finishReason = FinishReason::Cancelled;
    }
    readJsonAnswer(completion, call.format);
    return completion;
}

}  // namespace OpenAnima::Provider
