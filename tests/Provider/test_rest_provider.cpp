#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
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
using OpenAnima::Core::Role;
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

}  // namespace

// Catch2 assertions expand into do-while blocks and test cases are long by design, so
// the rules written for production code do not fit here.
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
    CHECK(completion.usage->promptTokens == 11);
    CHECK(completion.usage->completionTokens == 7);
    CHECK(completion.usage->totalTokens == 18);
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

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
