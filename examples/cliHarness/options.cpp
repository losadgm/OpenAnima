#include "cliHarness/options.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

#include "OpenAnima/Provider/RestProviderConfig.h"

#include "cliHarness/session.h"

namespace OpenAnimaCli {
namespace {

using OpenAnima::Provider::RestProviderConfig;

/// Version reported by --version, handed in by the build.
constexpr const char* Version = OPENANIMA_CLI_VERSION;

/// One option as it was written: "--model=gpt-4o" carries a value, "--model" does not.
struct WrittenOption {
    std::string name;                  ///< As typed, without the value.
    std::optional<std::string> value;  ///< What came after '=', when there was one.
};

/// Splits "--model=gpt-4o" into the name and the value.
/// @param argument Argument as typed.
/// @return The two halves, the value missing when the argument has no '='.
WrittenOption splitOption(const std::string& argument) {
    const std::size_t equals = argument.find('=');
    if (equals == std::string::npos) {
        return WrittenOption{argument, std::nullopt};
    }
    return WrittenOption{argument.substr(0, equals), argument.substr(equals + 1)};
}

/// @return The long name of an option, the short ones being aliases of it.
std::string canonicalName(const std::string& name) {
    if (name == "-u") {
        return "--base-url";
    }
    if (name == "-m") {
        return "--model";
    }
    if (name == "-k") {
        return "--api-key";
    }
    if (name == "-t") {
        return "--timeout";
    }
    if (name == "-r") {
        return "--raw";
    }
    if (name == "-h") {
        return "--help";
    }
    if (name == "-V") {
        return "--version";
    }
    return name;
}

/// @return What a value-taking option expects, for the usage line of an error.
std::string valueHint(const std::string& name) {
    if (name == "--base-url") {
        return "<url>";
    }
    if (name == "--model") {
        return "<name>";
    }
    if (name == "--api-key") {
        return "<token>";
    }
    if (name == "--timeout") {
        return "<seconds>";
    }
    if (name == "--temperature") {
        return "<value>";
    }
    if (name == "--max-tokens") {
        return "<count>";
    }
    if (name == "--provider") {
        return "<name>";
    }
    if (name == "--persona") {
        return "<text>";
    }
    return "<value>";
}

/// Renders a bound the way a person writes it, without trailing zeros.
std::string shortNumber(double value) {
    std::string text = std::to_string(value);
    text.erase(text.find_last_not_of('0') + 1, std::string::npos);
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

/// Reads an integer that has to fall inside a range.
/// @param option Option as typed, used to say what is wrong.
/// @param raw Value as typed.
/// @param minimum Lowest accepted value.
/// @param maximum Highest accepted value.
/// @return The value.
/// @throws UsageError when it is not an integer, or not one of those.
long readInteger(const std::string& option, const std::string& raw, long minimum, long maximum) {
    std::ostringstream expected;
    expected << option << " takes an integer between " << minimum << " and " << maximum << ", got '"
             << raw << "'";
    std::size_t consumed = 0;
    long value = 0;
    try {
        value = std::stol(raw, &consumed);
    } catch (const std::exception&) {
        throw UsageError(expected.str());
    }
    if (consumed != raw.size() || value < minimum || value > maximum) {
        throw UsageError(expected.str());
    }
    return value;
}

/// Reads a number that has to fall inside a range.
/// @param option Option as typed, used to say what is wrong.
/// @param raw Value as typed.
/// @param minimum Lowest accepted value.
/// @param maximum Highest accepted value.
/// @return The value.
/// @throws UsageError when it is not a number, or not one of those.
double readNumber(const std::string& option, const std::string& raw, double minimum,
                  double maximum) {
    std::ostringstream expected;
    expected << option << " takes a number between " << shortNumber(minimum) << " and "
             << shortNumber(maximum) << ", got '" << raw << "'";
    std::size_t consumed = 0;
    double value = 0.0;
    try {
        value = std::stod(raw, &consumed);
    } catch (const std::exception&) {
        throw UsageError(expected.str());
    }
    if (consumed != raw.size() || !std::isfinite(value) || value < minimum || value > maximum) {
        throw UsageError(expected.str());
    }
    return value;
}

/// @return The provider to use, checked against the ones that exist.
/// @throws UsageError when it is not one of them.
std::string readProvider(const std::string& raw) {
    if (raw == "rest") {
        return raw;
    }
    throw UsageError("--provider knows 'rest' only, got '" + raw + "'");
}

/// @param name Long name of an option.
/// @return Whether something has to follow it.
bool takesValue(const std::string& name) {
    return name == "--base-url" || name == "--model" || name == "--api-key" ||
           name == "--timeout" || name == "--temperature" || name == "--max-tokens" ||
           name == "--provider" || name == "--persona";
}

/// Applies an option that takes no value.
/// @param options Options to fill.
/// @param name Long name of the option.
/// @throws UsageError when the option does not exist.
void applyFlag(Options& options, const std::string& name) {
    if (name == "--help") {
        options.help = true;
    } else if (name == "--version") {
        options.version = true;
    } else if (name == "--raw") {
        options.raw = true;
    } else {
        throw UsageError("unknown option '" + name +
                         "'\n  try 'open_anima_cli --help' for the list of options");
    }
}

/// Applies an option that takes a value.
/// @param options Options to fill.
/// @param name Long name of the option.
/// @param value What followed it.
/// @throws UsageError when the value cannot be used.
void applyValue(Options& options, const std::string& name, const std::string& value) {
    if (name == "--base-url") {
        options.baseUrl = value;
    } else if (name == "--model") {
        options.model = value;
    } else if (name == "--api-key") {
        options.apiKey = value;
    } else if (name == "--timeout") {
        options.timeoutSeconds = readInteger(name, value, RestProviderConfig::MinTimeoutSeconds,
                                             RestProviderConfig::MaxTimeoutSeconds);
    } else if (name == "--temperature") {
        options.temperature = readNumber(name, value, RestProviderConfig::MinTemperature,
                                         RestProviderConfig::MaxTemperature);
    } else if (name == "--max-tokens") {
        options.maxTokens = static_cast<std::int32_t>(readInteger(
            name, value, RestProviderConfig::MinMaxTokens, RestProviderConfig::MaxMaxTokens));
    } else if (name == "--provider") {
        options.provider = readProvider(value);
    } else if (name == "--persona") {
        if (value.empty()) {
            throw UsageError("--persona needs a description\n  usage: --persona <text>");
        }
        options.persona = value;
    }
}

}  // namespace

Options parseArguments(const std::vector<std::string>& arguments) {
    Options options;
    std::vector<std::string> words;
    bool optionsEnded = false;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string& argument = arguments[index];
        if (!optionsEnded && argument == "--") {
            optionsEnded = true;
            continue;
        }
        if (optionsEnded || argument.size() < 2 || argument[0] != '-') {
            words.push_back(argument);
            continue;
        }

        const WrittenOption written = splitOption(argument);
        const std::string name = canonicalName(written.name);

        if (!takesValue(name)) {
            if (written.value.has_value()) {
                std::ostringstream message;
                message << name << " takes no value, got '" << *written.value
                        << "'\n  usage: " << name;
                throw UsageError(message.str());
            }
            applyFlag(options, name);
        } else if (written.value.has_value()) {
            applyValue(options, name, *written.value);
        } else if (index + 1 < arguments.size()) {
            ++index;
            applyValue(options, name, arguments[index]);
        } else {
            std::ostringstream message;
            message << name << " needs a value\n  usage: " << name << ' ' << valueHint(name);
            throw UsageError(message.str());
        }
    }

    std::string prompt;
    for (const std::string& word : words) {
        if (!prompt.empty()) {
            prompt += ' ';
        }
        prompt += word;
    }
    if (!prompt.empty()) {
        options.prompt = prompt;
    }
    return options;
}

void applyOptions(const Options& options, RestProviderConfig& config) {
    if (options.baseUrl.has_value()) {
        config.baseUrl = *options.baseUrl;
    }
    if (options.model.has_value()) {
        config.model = *options.model;
    }
    if (options.apiKey.has_value()) {
        config.apiKey = *options.apiKey;
    }
    if (options.timeoutSeconds.has_value()) {
        config.timeoutSeconds = *options.timeoutSeconds;
    }
    if (options.temperature.has_value()) {
        config.temperature = *options.temperature;
    }
    if (options.maxTokens.has_value()) {
        config.maxTokens = *options.maxTokens;
    }
}

std::string helpText() {
    std::string text;
    text += "open_anima_cli - talk to a chat-completions model from a shell\n";
    text += "\n";
    text += "usage:\n";
    text += "  open_anima_cli [options] [prompt words...]   send one prompt and leave\n";
    text += "  open_anima_cli [options]                     read lines from stdin\n";
    text += "\n";
    text += "options:\n";
    text += "  -u, --base-url <url>       API root, for example http://localhost:11434/v1\n";
    text += "  -m, --model <name>         model to ask\n";
    text += "  -k, --api-key <token>      bearer token, empty for servers without one\n";
    text += "  -t, --timeout <seconds>    longest silence allowed, 1 to 3600\n";
    text += "      --temperature <value>  sampling temperature, 0 to 2\n";
    text += "      --max-tokens <count>   reply length bound, 1 to 200000\n";
    text += "      --provider <name>      provider that answers, for now only 'rest'\n";
    text += "      --persona <text>       create the first npc and talk to it\n";
    text += "  -r, --raw                  print the whole reply as JSON after each turn\n";
    text += "  -h, --help                 this list\n";
    text += "  -V, --version              name and version\n";
    text += "      --                     everything after it is prompt text\n";
    text += "\n";
    text += "  An option wins over the variable it reads, OPENANIMA_CLI_<NAME>, and over\n";
    text += "  the .env file that sets it. Two of them are required, and come from either\n";
    text += "  side: --base-url and --model.\n";
    text += "\n";
    text += "  With prompt words the program sends them as one prompt and leaves. Without\n";
    text += "  them it reads lines from stdin: a line starting with / is a command, and\n";
    text += "  anything else goes to the npc that is active. There is no npc until one is\n";
    text += "  asked for, with --persona or with /create.\n";
    text += "\n";
    text += "commands:\n";

    std::size_t widest = 0;
    for (const CommandHelp& command : commandHelp()) {
        widest = std::max(widest, command.usage.size());
    }
    for (const CommandHelp& command : commandHelp()) {
        text += "  " + command.usage + std::string(widest - command.usage.size() + 2, ' ') +
                command.description + "\n";
    }

    text += "\n";
    text += "  ctrl-c stops the turn in flight and keeps what was said; twice, it leaves.\n";
    return text;
}

std::string versionText() {
    return std::string{"open_anima_cli "} + Version;
}

}  // namespace OpenAnimaCli
