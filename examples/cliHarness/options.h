/// @file
/// @brief The command line: what it says, and what it refuses.

#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "OpenAnima/Provider/RestProviderConfig.h"

namespace OpenAnimaCli {

/// The command line could not be understood. The message says what was expected.
class UsageError : public std::runtime_error {
public:
    /// @param message What was expected, ready to be printed.
    explicit UsageError(const std::string& message) : std::runtime_error(message) {}
};

/// Everything the program accepts on its command line.
///
/// A value given here wins over the variable it reads, and the variable wins over the
/// `.env` file, which is only a way of setting variables.
struct Options {
    bool help{false};                       ///< Print the manual and leave.
    bool version{false};                    ///< Print the version and leave.
    bool raw{false};                        ///< Print every reply as raw JSON.
    std::string provider{"rest"};           ///< Provider that answers the turns.
    std::optional<std::string> baseUrl;     ///< API root.
    std::optional<std::string> model;       ///< Model to ask.
    std::optional<std::string> apiKey;      ///< Bearer token.
    std::optional<long> timeoutSeconds;     ///< Longest silence allowed.
    std::optional<double> temperature;      ///< Sampling temperature.
    std::optional<std::int32_t> maxTokens;  ///< Reply length bound.
    std::optional<std::string> persona;     ///< Creates the first npc.
    std::optional<std::string> prompt;      ///< One-shot prompt, built from the words left.
};

/// Reads a command line.
/// @param arguments Arguments, without the program name.
/// @return What the program was told.
/// @throws UsageError when an option is unknown, has no value, or has one it cannot use.
Options parseArguments(const std::vector<std::string>& arguments);

/// Copies what the command line says into a configuration, leaving the rest alone.
/// @param options What the command line said.
/// @param config Configuration to fill, changed in place.
void applyOptions(const Options& options, OpenAnima::Provider::RestProviderConfig& config);

/// @return The manual, as printed by --help.
std::string helpText();

/// @return The name of the tool and the version it was built from.
std::string versionText();

}  // namespace OpenAnimaCli
