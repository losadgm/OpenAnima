#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "OpenAnima/Core/Types.h"
#include "OpenAnima/Errors.h"
#include "OpenAnima/Provider/ProviderConfig.h"
#include "OpenAnima/Provider/RestProvider.h"

#include "support/fake_http_transport.h"

using OpenAnima::ParseError;
using OpenAnima::ProviderError;
using OpenAnima::TransportError;
using OpenAnima::Core::ChatMessage;
using OpenAnima::Core::Role;
using OpenAnima::Provider::ProviderConfig;
using OpenAnima::Provider::RestProvider;
using OpenAnima::TestSupport::FakeHttpTransport;

namespace {

ProviderConfig localConfig(std::string apiKey = std::string{}) {
    ProviderConfig config;
    config.baseUrl = "http://localhost:11434/v1";
    config.model = "qwen2.5:7b";
    config.apiKey = std::move(apiKey);
    return config;
}

/// Builds the payload a chat-completions endpoint answers with.
std::string completionBody(const std::string& content) {
    nlohmann::json message;
    message["role"] = "assistant";
    message["content"] = content;

    nlohmann::json choice;
    choice["message"] = message;

    nlohmann::json payload;
    payload["choices"] = nlohmann::json::array({choice});
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

    const std::string reply =
        provider.complete({ChatMessage{Role::System, "be brief"}, ChatMessage{Role::User, "hi"}});

    CHECK(reply == "hello there");
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

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
