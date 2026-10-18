#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

#include "OpenAnima/Core/Types.h"
#include "OpenAnima/Errors.h"
#include "OpenAnima/Provider/RestProvider.h"
#include "OpenAnima/Provider/RestProviderConfig.h"

#include "support/fake_http_transport.h"

using OpenAnima::ParseError;
using OpenAnima::ProviderError;
using OpenAnima::TransportError;
using OpenAnima::Core::ChatMessage;
using OpenAnima::Core::Completion;
using OpenAnima::Core::FinishReason;
using OpenAnima::Core::ResponseFormat;
using OpenAnima::Core::Role;
using OpenAnima::Core::TokenUsage;
using OpenAnima::Provider::Call;
using OpenAnima::Provider::RestProvider;
using OpenAnima::Provider::RestProviderConfig;
using OpenAnima::TestSupport::FakeHttpTransport;

namespace {

RestProviderConfig localConfig(std::string apiKey = std::string{}) {
    RestProviderConfig config;
    config.baseUrl = "http://localhost:11434/v1";
    config.model = "qwen2.5:7b";
    config.apiKey = std::move(apiKey);
    return config;
}

/// Builds the payload a chat-completions endpoint answers with.
std::string completionBody(const std::string& content, const char* finishReason = "stop") {
    nlohmann::json message;
    message["role"] = "assistant";
    message["content"] = content;

    nlohmann::json choice;
    choice["message"] = message;
    choice["finish_reason"] = finishReason;

    nlohmann::json payload;
    payload["choices"] = nlohmann::json::array({choice});
    payload["model"] = "qwen2.5:7b";
    return payload.dump();
}

std::vector<ChatMessage> oneTurn(const std::string& text) {
    return {ChatMessage{Role::User, text}};
}

/// Builds one chunk of a streamed answer, which is an event carrying a piece of the text.
/// @param text Piece of the answer the chunk carries.
/// @return The chunk, as it arrives in the body of the response.
std::string deltaChunk(const std::string& text) {
    nlohmann::json delta;
    delta["content"] = text;
    nlohmann::json choice;
    choice["delta"] = delta;
    nlohmann::json payload;
    payload["choices"] = nlohmann::json::array({choice});
    return "data: " + payload.dump() + "\n\n";
}

}  // namespace

// Catch2 assertions expand into do-while blocks, test cases are long by design, and the
// analyzer cannot see through REQUIRE(), so an optional it has just seen checked still
// looks unchecked to it. None of the three fits production code.
// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("RestProvider sends the chat-completions contract", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->response = {200, completionBody("hello there")};
    RestProvider provider{localConfig(), transport};

    const Completion completion =
        provider.complete({ChatMessage{Role::System, "be brief"}, ChatMessage{Role::User, "hi"}});

    CHECK(completion.text == "hello there");
    REQUIRE(transport->requests.size() == 1);
    CHECK(transport->requests[0].url == "http://localhost:11434/v1/chat/completions");
    CHECK(transport->lastHeader("Content-Type") == "application/json");

    const nlohmann::json body = nlohmann::json::parse(transport->lastBody());
    CHECK(body["model"] == "qwen2.5:7b");
    CHECK(body["stream"] == false);
    CHECK(body["temperature"] == 0.7);
    CHECK(body["max_tokens"] == 512);
    REQUIRE(body["messages"].size() == 2);
    CHECK(body["messages"][0]["role"] == "system");
    CHECK(body["messages"][0]["content"] == "be brief");
    CHECK(body["messages"][1]["role"] == "user");
    CHECK(body["messages"][1]["content"] == "hi");
}

TEST_CASE("RestProvider signs the request only when an API key is set", "[provider]") {
    auto anonymous = std::make_shared<FakeHttpTransport>();
    anonymous->response = {200, completionBody("ok")};
    RestProvider withoutKey{localConfig(), anonymous};
    (void)withoutKey.complete(oneTurn("hi"));
    CHECK(anonymous->lastHeader("Authorization").empty());

    auto authenticated = std::make_shared<FakeHttpTransport>();
    authenticated->response = {200, completionBody("ok")};
    RestProvider withKey{localConfig("secret"), authenticated};
    (void)withKey.complete(oneTurn("hi"));
    CHECK(authenticated->lastHeader("Authorization") == "Bearer secret");
}

TEST_CASE("RestProvider reports the status of a rejected call", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->response = {401, R"({"error":{"message":"invalid api key"}})"};
    RestProvider provider{localConfig(), transport};

    try {
        (void)provider.complete(oneTurn("hi"));
        FAIL("expected a ProviderError");
    } catch (const ProviderError& error) {
        CHECK(error.statusCode() == 401);
        CHECK(std::string{error.what()}.find("invalid api key") != std::string::npos);
    }
}

TEST_CASE("RestProvider surfaces an error payload served with HTTP 200", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->response = {200, R"({"error":{"message":"model not found"}})"};
    RestProvider provider{localConfig(), transport};

    CHECK_THROWS_AS(provider.complete(oneTurn("hi")), ProviderError);
}

TEST_CASE("RestProvider rejects a body that is not JSON", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->response = {200, "<html>gateway timeout</html>"};
    RestProvider provider{localConfig(), transport};

    CHECK_THROWS_AS(provider.complete(oneTurn("hi")), ParseError);
}

TEST_CASE("RestProvider rejects a payload without usable choices", "[provider]") {
    const std::vector<std::string> malformed = {
        R"({"id":"cmpl-1"})",                                 // no choices at all
        R"({"choices":[]})",                                  // empty choices
        R"({"choices":[{"message":{"role":"assistant"}}]})",  // no content
        R"({"choices":[{"message":{"content":7}}]})",         // content is not text
    };

    for (const std::string& payload : malformed) {
        auto transport = std::make_shared<FakeHttpTransport>();
        transport->response = {200, payload};
        RestProvider provider{localConfig(), transport};

        INFO("payload " << payload);
        CHECK_THROWS_AS(provider.complete(oneTurn("hi")), ParseError);
    }
}

TEST_CASE("RestProvider propagates transport failures", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->failure = "connection refused";
    RestProvider provider{localConfig(), transport};

    CHECK_THROWS_AS(provider.complete(oneTurn("hi")), TransportError);
}

TEST_CASE("RestProvider keeps the configuration it was built with", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    RestProvider provider{localConfig("secret"), transport};

    CHECK(provider.config().model == "qwen2.5:7b");
    CHECK(provider.config().apiKey == "secret");
}

TEST_CASE("RestProvider reads the metadata of the answer", "[provider]") {
    nlohmann::json message;
    message["role"] = "assistant";
    message["content"] = "hello";

    nlohmann::json choice;
    choice["message"] = message;
    choice["finish_reason"] = "stop";

    nlohmann::json usage;
    usage["prompt_tokens"] = 11;
    usage["completion_tokens"] = 7;
    usage["total_tokens"] = 18;

    nlohmann::json payload;
    payload["choices"] = nlohmann::json::array({choice});
    payload["model"] = "gpt-4o-2024-08-06";
    payload["usage"] = usage;

    auto transport = std::make_shared<FakeHttpTransport>();
    transport->response = {200, payload.dump()};
    RestProvider provider{localConfig(), transport};

    const Completion completion = provider.complete(oneTurn("hi"));

    CHECK(completion.text == "hello");
    CHECK(completion.model == "gpt-4o-2024-08-06");
    CHECK(completion.finishReason == FinishReason::Stop);
    REQUIRE(completion.usage.has_value());
    // The assertion above is the check; the analyzer cannot see through it.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    const TokenUsage& reported = completion.usage.value();
    CHECK(reported.promptTokens == 11);
    CHECK(reported.completionTokens == 7);
    CHECK(reported.totalTokens == 18);
}

TEST_CASE("RestProvider tolerates an answer without metadata", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->response = {200, R"({"choices":[{"message":{"role":"assistant","content":"hi"}}]})"};
    RestProvider provider{localConfig(), transport};

    const Completion completion = provider.complete(oneTurn("hi"));

    CHECK(completion.text == "hi");
    CHECK(completion.model.empty());
    CHECK(completion.finishReason == FinishReason::Unknown);
    CHECK_FALSE(completion.usage.has_value());
}

TEST_CASE("RestProvider maps the finish reasons it knows", "[provider]") {
    const auto reasonOf = [](const std::string& name) {
        auto transport = std::make_shared<FakeHttpTransport>();
        transport->response = {200, completionBody("hi", name.c_str())};
        RestProvider provider{localConfig(), transport};
        return provider.complete(oneTurn("hi")).finishReason;
    };

    CHECK(reasonOf("stop") == FinishReason::Stop);
    CHECK(reasonOf("length") == FinishReason::Length);
    CHECK(reasonOf("tool_calls") == FinishReason::ToolCalls);
    CHECK(reasonOf("content_filter") == FinishReason::ContentFilter);
    CHECK(reasonOf("something-the-model-invented") == FinishReason::Unknown);
}

TEST_CASE("RestProvider tolerates a null finish reason", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->response = {200,
                           R"({"choices":[{"message":{"content":"hi"},"finish_reason":null}]})"};
    RestProvider provider{localConfig(), transport};

    CHECK(provider.complete(oneTurn("hi")).finishReason == FinishReason::Unknown);
}

TEST_CASE("RestProvider assembles a streamed answer and reports every piece", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    // The pieces land in separate pieces of the body, which is the shape a socket
    // hands over and the one a reader written against whole lines gets wrong.
    transport->chunks = {
        "data: {\"choices\":[{\"delta\":{\"role\":\"assistant\",\"content\":\"\"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\"Halt\"}}]}\n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\"!\"}}]}\n\ndata: "
        "{\"choices\":[{\"delta\":{},",
        "\"finish_reason\":\"stop\"}],\"model\":\"qwen2.5:7b\"}\n\ndata: [DONE]\n\n"};
    RestProvider provider{localConfig(), transport};

    std::vector<std::string> pieces;
    const Completion completion = provider.complete(
        oneTurn("hi"), [&pieces](std::string_view piece) { pieces.emplace_back(piece); });

    CHECK(completion.text == "Halt!");
    CHECK(completion.model == "qwen2.5:7b");
    CHECK(completion.finishReason == FinishReason::Stop);
    REQUIRE(pieces.size() == 2);
    CHECK(pieces[0] == "Halt");
    CHECK(pieces[1] == "!");
}

TEST_CASE("RestProvider ignores the heartbeats a server sends while it thinks", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    // A `data` field with nothing in it is what a server sends to hold the connection
    // open. Reading it as a chunk ended the turn with a JSON error.
    transport->chunks = {
        "data:\n\n", "data: {\"choices\":[{\"delta\":{\"content\":\"Halt\"}}]}\n\n", "data: \n\n",
        "data: {\"choices\":[{\"delta\":{\"content\":\"!\"},\"finish_reason\":\"stop\"}]}\n\n"
        "data: [DONE]\n\n"};
    RestProvider provider{localConfig(), transport};

    std::vector<std::string> pieces;
    const Completion completion = provider.complete(
        oneTurn("hi"), [&pieces](std::string_view piece) { pieces.emplace_back(piece); });

    CHECK(completion.text == "Halt!");
    CHECK(completion.finishReason == FinishReason::Stop);
    REQUIRE(pieces.size() == 2);
    CHECK(pieces[0] == "Halt");
    CHECK(pieces[1] == "!");
}

TEST_CASE("RestProvider asks for a stream only when the caller wants the pieces", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->response = {200, completionBody("hello there")};
    RestProvider provider{localConfig(), transport};

    SECTION("without a sink") {
        provider.complete(oneTurn("hi"));

        const nlohmann::json body = nlohmann::json::parse(transport->lastBody());
        CHECK(body["stream"] == false);
        CHECK_FALSE(body.contains("stream_options"));
        CHECK(transport->lastHeader("Accept") == "application/json");
    }

    SECTION("with a sink") {
        provider.complete(oneTurn("hi"), [](std::string_view) {});

        const nlohmann::json body = nlohmann::json::parse(transport->lastBody());
        CHECK(body["stream"] == true);
        // Without this the answer comes back with no token usage at all.
        CHECK(body["stream_options"]["include_usage"] == true);
        CHECK(transport->lastHeader("Accept") == "text/event-stream");
    }
}

TEST_CASE("RestProvider reads the usage of a chunk that carries only that", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    // The chunk that closes a stream reports an empty choices array and the usage, and
    // it arrives after the finish reason.
    transport->chunks = {
        "data: {\"choices\":[{\"delta\":{\"content\":\"ok\"},\"finish_reason\":\"stop\"}]}\n\n",
        "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":14,\"completion_tokens\":3}}\n\n",
        "data: [DONE]\n\n"};
    RestProvider provider{localConfig(), transport};

    const Completion completion = provider.complete(oneTurn("hi"), [](std::string_view) {});

    CHECK(completion.text == "ok");
    REQUIRE(completion.usage.has_value());
    // The assertion above is the check; the analyzer cannot see through it.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    const TokenUsage& reported = completion.usage.value();
    CHECK(reported.promptTokens == 14);
    CHECK(reported.completionTokens == 3);
}

TEST_CASE("RestProvider stops reading at the sentinel", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    // Anything after the sentinel is not part of the answer, and a server that keeps
    // talking after it must not be believed.
    transport->chunks = {"data: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n\n",
                         "data: [DONE]\n\n",
                         "data: {\"choices\":[{\"delta\":{\"content\":\" and more\"}}]}\n\n"};
    RestProvider provider{localConfig(), transport};

    const Completion completion = provider.complete(oneTurn("hi"), [](std::string_view) {});

    CHECK(completion.text == "ok");
}

TEST_CASE("RestProvider reports an error streamed in the middle of an answer", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->chunks = {"data: {\"choices\":[{\"delta\":{\"content\":\"half\"}}]}\n\n",
                         "data: {\"error\":{\"message\":\"rate limit\"}}\n\n"};
    RestProvider provider{localConfig(), transport};

    std::vector<std::string> pieces;
    CHECK_THROWS_AS(
        provider.complete(oneTurn("hi"),
                          [&pieces](std::string_view piece) { pieces.emplace_back(piece); }),
        ProviderError);
    // What arrived before the failure is real and stays with the caller.
    REQUIRE(pieces.size() == 1);
    CHECK(pieces[0] == "half");
}

TEST_CASE("RestProvider refuses a streamed answer that is not JSON", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->chunks = {"data: not json at all\n\n"};
    RestProvider provider{localConfig(), transport};

    CHECK_THROWS_AS(provider.complete(oneTurn("hi"), [](std::string_view) {}), ParseError);
}

TEST_CASE("RestProvider reports the status of a rejected stream", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->response = {401, R"({"error":{"message":"bad key"}})"};
    RestProvider provider{localConfig("wrong"), transport};

    try {
        provider.complete(oneTurn("hi"), [](std::string_view) {});
        FAIL("the call should have reported the status");
    } catch (const ProviderError& error) {
        CHECK(error.statusCode() == 401);
        CHECK(std::string(error.what()).find("bad key") != std::string::npos);
    }
}

TEST_CASE("RestProvider marks an answer the caller abandoned", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->chunks = {"data: {\"choices\":[{\"delta\":{\"content\":\"first\"}}]}\n\n",
                         "data: {\"choices\":[{\"delta\":{\"content\":\" second\"}}]}\n\n"};
    RestProvider provider{localConfig(), transport};

    bool stop = false;
    std::vector<std::string> pieces;
    const Completion completion =
        provider.complete(oneTurn("hi"), Call{[&pieces, &stop](std::string_view piece) {
                                                  pieces.emplace_back(piece);
                                                  stop = true;  // the caller decides to stop here
                                              },
                                              [&stop] { return !stop; },
                                              {}});

    // What had been produced is real and comes back, marked for what it is: not a
    // failure, but not a finished answer either.
    CHECK(completion.text == "first");
    CHECK(completion.finishReason == FinishReason::Cancelled);
    REQUIRE(pieces.size() == 1);
    CHECK(pieces[0] == "first");
}

TEST_CASE("RestProvider does not call an abandoned call a failure", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    // A transfer cut before the headers is what a real transport reports as status zero and
    // nothing in the body.
    transport->response = {0, ""};
    transport->chunks = {""};
    RestProvider provider{localConfig(), transport};

    const Completion completion =
        provider.complete(oneTurn("hi"), Call{{}, [] { return false; }, {}});

    CHECK(completion.finishReason == FinishReason::Cancelled);
    CHECK(completion.text.empty());
}

TEST_CASE("RestProvider asks for a JSON object when one is wanted", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->response = {200, completionBody(R"({"reasoning":"why","dialogue":"hello"})")};
    RestProvider provider{localConfig(), transport};

    const Completion completion =
        provider.complete(oneTurn("hi"), Call{{}, {}, ResponseFormat::jsonObject()});

    const nlohmann::json body = nlohmann::json::parse(transport->lastBody());
    REQUIRE(body.contains("response_format"));
    CHECK(body["response_format"]["type"] == "json_object");

    // The answer is the JSON that was asked for, and it arrives read as well as raw.
    REQUIRE(completion.json.has_value());
    // The REQUIRE above is the check.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    const nlohmann::ordered_json& answer = *completion.json;
    CHECK(answer["dialogue"] == "hello");
    CHECK(completion.text == R"({"reasoning":"why","dialogue":"hello"})");
}

TEST_CASE("RestProvider sends the schema it was given", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->response = {200, completionBody(R"({"dialogue":"hello"})")};
    RestProvider provider{localConfig(), transport};

    const nlohmann::json schema = {{"type", "object"},
                                   {"properties", {{"dialogue", {{"type", "string"}}}}},
                                   {"required", nlohmann::json::array({"dialogue"})}};
    (void)provider.complete(oneTurn("hi"), Call{{}, {}, ResponseFormat::jsonSchema(schema)});

    const nlohmann::json body = nlohmann::json::parse(transport->lastBody());
    REQUIRE(body.contains("response_format"));
    CHECK(body["response_format"]["type"] == "json_schema");
    CHECK(body["response_format"]["json_schema"]["schema"] == schema);
}

TEST_CASE("RestProvider refuses an answer that is not the JSON it asked for", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->response = {200, completionBody("I would rather talk")};
    RestProvider provider{localConfig(), transport};

    CHECK_THROWS_AS(provider.complete(oneTurn("hi"), Call{{}, {}, ResponseFormat::jsonObject()}),
                    ParseError);
}

TEST_CASE("RestProvider reads a streamed answer as JSON too", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->chunks = {deltaChunk(R"({"dialogue":")"), deltaChunk("hi"), deltaChunk(R"("})"),
                         "data: [DONE]\n\n"};
    RestProvider provider{localConfig(), transport};

    std::vector<std::string> pieces;
    const Completion completion = provider.complete(
        oneTurn("hi"), Call{[&pieces](std::string_view piece) { pieces.emplace_back(piece); },
                            {},
                            ResponseFormat::jsonObject()});

    // The pieces are the raw JSON of the answer, and the whole of it is read at the end.
    REQUIRE(pieces.size() == 3);
    REQUIRE(completion.json.has_value());
    // The REQUIRE above is the check.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    const nlohmann::ordered_json& answer = *completion.json;
    CHECK(answer["dialogue"] == "hi");
}

TEST_CASE("RestProvider leaves a cancelled turn unread", "[provider]") {
    auto transport = std::make_shared<FakeHttpTransport>();
    transport->chunks = {deltaChunk(R"({"dialogue":")"), deltaChunk("half")};
    RestProvider provider{localConfig(), transport};

    bool stop = false;
    const Completion completion = provider.complete(
        oneTurn("hi"), Call{[&stop](std::string_view) { stop = true; }, [&stop] { return !stop; },
                            ResponseFormat::jsonObject()});

    // Half a JSON document is not an answer, and the caller stopped the turn itself: what
    // it had is kept as text, and nothing is read out of it.
    CHECK(completion.finishReason == FinishReason::Cancelled);
    CHECK_FALSE(completion.json.has_value());
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
