#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <memory>
#include <string>

#include "OpenAnima/Errors.h"
#include "OpenAnima/Provider/RestProvider.h"
#include "OpenAnima/Provider/RestProviderConfig.h"

#include "support/fake_http_transport.h"

using OpenAnima::ConfigError;
using OpenAnima::Provider::RestProvider;
using OpenAnima::Provider::RestProviderConfig;
using OpenAnima::TestSupport::FakeHttpTransport;

namespace {

RestProviderConfig localConfig() {
    RestProviderConfig config;
    config.baseUrl = "http://localhost:11434/v1";
    config.model = "qwen2.5:7b";
    config.apiKey = "secret";
    return config;
}

}  // namespace

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
TEST_CASE("a configuration with only an endpoint and a model is valid", "[config]") {
    RestProviderConfig config;
    config.baseUrl = "http://localhost:11434/v1";
    config.model = "qwen2.5:7b";

    REQUIRE(config.timeoutSeconds == 60);
    REQUIRE(config.temperature == 0.7);
    REQUIRE(config.maxTokens == 512);
    REQUIRE_NOTHROW(config.validate());
}

TEST_CASE("validate accepts the edges of every range", "[config]") {
    RestProviderConfig config = localConfig();
    config.timeoutSeconds = RestProviderConfig::MinTimeoutSeconds;
    config.temperature = RestProviderConfig::MinTemperature;
    config.maxTokens = RestProviderConfig::MinMaxTokens;
    REQUIRE_NOTHROW(config.validate());

    config.timeoutSeconds = RestProviderConfig::MaxTimeoutSeconds;
    config.temperature = RestProviderConfig::MaxTemperature;
    config.maxTokens = RestProviderConfig::MaxMaxTokens;
    REQUIRE_NOTHROW(config.validate());
}

TEST_CASE("validate rejects a timeout outside its range", "[config]") {
    RestProviderConfig config = localConfig();

    config.timeoutSeconds = RestProviderConfig::MinTimeoutSeconds - 1;
    REQUIRE_THROWS_AS(config.validate(), ConfigError);

    config.timeoutSeconds = RestProviderConfig::MaxTimeoutSeconds + 1;
    REQUIRE_THROWS_AS(config.validate(), ConfigError);
}

TEST_CASE("validate rejects a temperature outside its range", "[config]") {
    RestProviderConfig config = localConfig();

    config.temperature = RestProviderConfig::MinTemperature - 0.1;
    REQUIRE_THROWS_AS(config.validate(), ConfigError);

    config.temperature = RestProviderConfig::MaxTemperature + 0.1;
    REQUIRE_THROWS_AS(config.validate(), ConfigError);
}

TEST_CASE("validate rejects a temperature that is not a number", "[config]") {
    RestProviderConfig config = localConfig();

    config.temperature = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_THROWS_AS(config.validate(), ConfigError);

    config.temperature = std::numeric_limits<double>::infinity();
    REQUIRE_THROWS_AS(config.validate(), ConfigError);
}

TEST_CASE("validate rejects a maxTokens outside its range", "[config]") {
    RestProviderConfig config = localConfig();

    config.maxTokens = RestProviderConfig::MinMaxTokens - 1;
    REQUIRE_THROWS_AS(config.validate(), ConfigError);

    config.maxTokens = RestProviderConfig::MaxMaxTokens + 1;
    REQUIRE_THROWS_AS(config.validate(), ConfigError);
}

TEST_CASE("the error names the setting that is wrong", "[config]") {
    RestProviderConfig config = localConfig();
    config.timeoutSeconds = 0;

    try {
        config.validate();
        FAIL("validate accepted a zero timeout");
    } catch (const ConfigError& error) {
        const std::string message = error.what();
        REQUIRE(message.find("timeout") != std::string::npos);
        REQUIRE(message.find("3600") != std::string::npos);
    }
}

TEST_CASE("the completions URL is the base URL plus the route", "[config]") {
    RestProviderConfig config = localConfig();
    REQUIRE(config.chatCompletionsUrl() == "http://localhost:11434/v1/chat/completions");

    config.baseUrl = "http://localhost:11434/v1/";
    REQUIRE(config.chatCompletionsUrl() == "http://localhost:11434/v1/chat/completions");

    config.baseUrl = "https://example.test/v1///";
    REQUIRE(config.chatCompletionsUrl() == "https://example.test/v1/chat/completions");
}

TEST_CASE("a provider refuses a configuration it cannot use", "[config]") {
    RestProviderConfig config = localConfig();
    config.maxTokens = 0;

    REQUIRE_THROWS_AS((RestProvider{config, std::make_shared<FakeHttpTransport>()}), ConfigError);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
