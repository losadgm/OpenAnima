#include <catch2/catch_test_macros.hpp>
#include <string>
#include <vector>

#include "cliHarness/options.h"
#include "cliHarness/session.h"

using OpenAnima::Provider::RestProviderConfig;
using OpenAnimaCli::applyOptions;
using OpenAnimaCli::commandHelp;
using OpenAnimaCli::helpText;
using OpenAnimaCli::Options;
using OpenAnimaCli::parseArguments;
using OpenAnimaCli::UsageError;
using OpenAnimaCli::versionText;

namespace {

/// Renders the message of a usage error, so a case can say what it expects to be told.
template <typename Callable>
std::string refusal(Callable callable) {
    try {
        callable();
    } catch (const UsageError& error) {
        return error.what();
    }
    return std::string{};
}

RestProviderConfig configured() {
    RestProviderConfig config;
    config.baseUrl = "http://from-the-environment/v1";
    config.model = "model-from-the-environment";
    config.apiKey = "key-from-the-environment";
    return config;
}

}  // namespace

// NOLINTBEGIN(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
TEST_CASE("an empty command line asks for nothing", "[cli]") {
    const Options options = parseArguments({});

    REQUIRE_FALSE(options.help);
    REQUIRE_FALSE(options.version);
    REQUIRE_FALSE(options.raw);
    REQUIRE(options.provider == "rest");
    REQUIRE_FALSE(options.baseUrl.has_value());
    REQUIRE_FALSE(options.model.has_value());
    REQUIRE_FALSE(options.apiKey.has_value());
    REQUIRE_FALSE(options.timeoutSeconds.has_value());
    REQUIRE_FALSE(options.temperature.has_value());
    REQUIRE_FALSE(options.maxTokens.has_value());
    REQUIRE_FALSE(options.persona.has_value());
    REQUIRE_FALSE(options.prompt.has_value());
}

TEST_CASE("the short names are the long ones", "[cli]") {
    const Options options = parseArguments(
        {"-u", "http://localhost:11434/v1", "-m", "qwen2.5:7b", "-k", "", "-t", "30", "-r"});

    REQUIRE(options.baseUrl == "http://localhost:11434/v1");
    REQUIRE(options.model == "qwen2.5:7b");
    REQUIRE(options.apiKey == "");
    REQUIRE(options.timeoutSeconds == 30);
    REQUIRE(options.raw);
}

TEST_CASE("a value can come after an equals sign", "[cli]") {
    const Options options = parseArguments(
        {"--base-url=https://api.openai.com/v1", "--model=gpt-4o", "--temperature=0.2"});

    REQUIRE(options.baseUrl == "https://api.openai.com/v1");
    REQUIRE(options.model == "gpt-4o");
    REQUIRE(options.temperature == 0.2);
}

TEST_CASE("the words that are not options are the prompt", "[cli]") {
    const Options options =
        parseArguments({"--model", "gpt-4o", "do", "you", "have", "any", "daggers?"});

    REQUIRE(options.prompt == "do you have any daggers?");
}

TEST_CASE("the words after -- are prompt text, even when they look like options", "[cli]") {
    const Options options = parseArguments({"-m", "gpt-4o", "--persona", "herrero", "--", "--model",
                                            "is", "part", "of", "the", "prompt"});

    REQUIRE(options.persona == "herrero");
    REQUIRE(options.model == "gpt-4o");
    REQUIRE(options.prompt == "--model is part of the prompt");
}

TEST_CASE("help, version and raw take no value", "[cli]") {
    const Options options = parseArguments({"-h"});
    REQUIRE(options.help);

    REQUIRE(parseArguments({"-V"}).version);
    REQUIRE(parseArguments({"--help"}).help);

    const std::string message = refusal([] { parseArguments({"--raw=on"}); });
    REQUIRE(message.find("--raw takes no value") != std::string::npos);
}

TEST_CASE("an unknown option says what to do", "[cli]") {
    const std::string message = refusal([] { parseArguments({"--colour"}); });

    REQUIRE(message.find("unknown option '--colour'") != std::string::npos);
    REQUIRE(message.find("--help") != std::string::npos);
}

TEST_CASE("a value is asked for by name", "[cli]") {
    const std::string message = refusal([] { parseArguments({"--model"}); });

    REQUIRE(message.find("--model needs a value") != std::string::npos);
    REQUIRE(message.find("usage: --model <name>") != std::string::npos);
}

TEST_CASE("a number out of its range is refused with the range in the message", "[cli]") {
    const std::string temperature = refusal([] { parseArguments({"--temperature", "3"}); });
    REQUIRE(temperature.find("--temperature takes a number between 0 and 2") != std::string::npos);

    const std::string timeout = refusal([] { parseArguments({"--timeout", "0"}); });
    REQUIRE(timeout.find("--timeout takes an integer between 1 and 3600") != std::string::npos);

    const std::string tokens = refusal([] { parseArguments({"--max-tokens", "many"}); });
    REQUIRE(tokens.find("--max-tokens takes an integer between 1 and 200000") != std::string::npos);
}

TEST_CASE("only the providers that exist are accepted", "[cli]") {
    REQUIRE(parseArguments({"--provider", "rest"}).provider == "rest");

    const std::string message = refusal([] { parseArguments({"--provider", "grpc"}); });
    REQUIRE(message.find("--provider knows 'rest' only") != std::string::npos);
    REQUIRE(message.find("grpc") != std::string::npos);
}

TEST_CASE("a persona has to say who the npc is", "[cli]") {
    const std::string message = refusal([] { parseArguments({"--persona", ""}); });

    REQUIRE(message.find("--persona needs a description") != std::string::npos);
    REQUIRE(message.find("usage: --persona <text>") != std::string::npos);
}

TEST_CASE("the command line wins over the configuration it is given", "[cli]") {
    RestProviderConfig config = configured();
    applyOptions(parseArguments({"--model", "gpt-4o"}), config);

    REQUIRE(config.model == "gpt-4o");
    REQUIRE(config.baseUrl == "http://from-the-environment/v1");
    REQUIRE(config.apiKey == "key-from-the-environment");
}

TEST_CASE("an option that was not given leaves the value alone", "[cli]") {
    RestProviderConfig config = configured();
    config.timeoutSeconds = 120;
    config.temperature = 0.1;
    config.maxTokens = 64;
    applyOptions(parseArguments({}), config);

    REQUIRE(config.timeoutSeconds == 120);
    REQUIRE(config.temperature == 0.1);
    REQUIRE(config.maxTokens == 64);
}

TEST_CASE("every option the parser knows is documented", "[cli]") {
    const std::string manual = helpText();
    const std::vector<std::string> options = {
        "--base-url", "--model",   "--api-key", "--timeout", "--temperature", "--max-tokens",
        "--provider", "--persona", "--raw",     "--help",    "--version",
    };

    for (const std::string& option : options) {
        REQUIRE(manual.find(option) != std::string::npos);
    }
}

TEST_CASE("the manual documents every command", "[cli]") {
    const std::string manual = helpText();
    for (const OpenAnimaCli::CommandHelp& command : commandHelp()) {
        REQUIRE(manual.find(command.usage) != std::string::npos);
    }
}

TEST_CASE("the version names the tool", "[cli]") {
    REQUIRE(versionText().rfind("open_anima_cli ", 0) == 0);
}
// NOLINTEND(cppcoreguidelines-avoid-do-while, readability-function-cognitive-complexity)
