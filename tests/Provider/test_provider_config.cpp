#include <catch2/catch_test_macros.hpp>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "OpenAnima/Errors.h"
#include "OpenAnima/Provider/ProviderConfig.h"

using OpenAnima::ConfigError;
using OpenAnima::Provider::ProviderConfig;

namespace {

/// Builds a lookup over a fixed set of variables.
ProviderConfig::EnvironmentLookup lookupOf(std::map<std::string, std::string> values) {
    return [values = std::move(values)](const char* name) -> std::string {
        const auto entry = values.find(name);
        return entry == values.end() ? std::string{} : entry->second;
    };
}

/// @return The two variables the library cannot work without.
std::map<std::string, std::string> minimalValues() {
    return {{"OPENANIMA_BASE_URL", "http://localhost:11434/v1"}, {"OPENANIMA_MODEL", "qwen2.5:7b"}};
}

}  // namespace

// Catch2 assertions expand into do-while blocks and test cases are long by design, so
// the rules written for production code do not fit here.
// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)

TEST_CASE("ProviderConfig::fromEnvironment reads the required variables", "[config]") {
    const ProviderConfig config = ProviderConfig::fromEnvironment(lookupOf(minimalValues()));

    CHECK(config.baseUrl == "http://localhost:11434/v1");
    CHECK(config.model == "qwen2.5:7b");
    CHECK(config.apiKey.empty());
}

TEST_CASE("ProviderConfig::fromEnvironment applies the documented defaults", "[config]") {
    const ProviderConfig config = ProviderConfig::fromEnvironment(lookupOf(minimalValues()));

    CHECK(config.timeoutSeconds == 60);
    CHECK(config.temperature == 0.7);
    CHECK(config.maxTokens == 512);
}

TEST_CASE("ProviderConfig::fromEnvironment reads the optional variables", "[config]") {
    std::map<std::string, std::string> values = minimalValues();
    values["OPENANIMA_API_KEY"] = "secret";
    values["OPENANIMA_TIMEOUT_SECONDS"] = "5";
    values["OPENANIMA_TEMPERATURE"] = "0.2";
    values["OPENANIMA_MAX_TOKENS"] = "128";

    const ProviderConfig config = ProviderConfig::fromEnvironment(lookupOf(values));

    CHECK(config.apiKey == "secret");
    CHECK(config.timeoutSeconds == 5);
    CHECK(config.temperature == 0.2);
    CHECK(config.maxTokens == 128);
}

TEST_CASE("ProviderConfig::fromEnvironment requires a base URL and a model", "[config]") {
    std::map<std::string, std::string> withoutBaseUrl = minimalValues();
    withoutBaseUrl.erase("OPENANIMA_BASE_URL");

    std::map<std::string, std::string> withoutModel = minimalValues();
    withoutModel.erase("OPENANIMA_MODEL");

    CHECK_THROWS_AS(ProviderConfig::fromEnvironment(lookupOf(withoutBaseUrl)), ConfigError);
    CHECK_THROWS_AS(ProviderConfig::fromEnvironment(lookupOf(withoutModel)), ConfigError);
}

TEST_CASE("ProviderConfig::fromEnvironment rejects malformed numbers", "[config]") {
    const std::vector<std::pair<std::string, std::string>> cases = {
        {"OPENANIMA_TIMEOUT_SECONDS", "soon"},  // not a number
        {"OPENANIMA_TIMEOUT_SECONDS", "0"},     // below the minimum
        {"OPENANIMA_TEMPERATURE", "0.7 "},      // trailing garbage
        {"OPENANIMA_TEMPERATURE", "3"},         // above the maximum
        {"OPENANIMA_MAX_TOKENS", "512.5"},      // not an integer
        {"OPENANIMA_MAX_TOKENS", "-1"},         // below the minimum
    };

    for (const std::pair<std::string, std::string>& current : cases) {
        std::map<std::string, std::string> values = minimalValues();
        values[current.first] = current.second;

        INFO("variable " << current.first << " set to '" << current.second << "'");
        CHECK_THROWS_AS(ProviderConfig::fromEnvironment(lookupOf(values)), ConfigError);
    }
}

TEST_CASE("ProviderConfig::chatCompletionsUrl normalises the trailing slash", "[config]") {
    ProviderConfig config;
    config.baseUrl = "http://localhost:11434/v1/";
    CHECK(config.chatCompletionsUrl() == "http://localhost:11434/v1/chat/completions");

    config.baseUrl = "http://localhost:11434/v1";
    CHECK(config.chatCompletionsUrl() == "http://localhost:11434/v1/chat/completions");
}

// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
