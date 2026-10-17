/// @file
/// @brief Reads a client's API settings from its own environment variables.

#pragma once

#include <functional>
#include <string>

#include "OpenAnima/Provider/RestProviderConfig.h"

namespace OpenAnimaExample {

/// Resolves an environment variable name to its value.
/// Implementations return an empty string when the variable is not set.
using Lookup = std::function<std::string(const char*)>;

/// Copies the variables of one client into a configuration, leaving the rest alone.
///
/// The variables are `<prefix>_BASE_URL`, `<prefix>_MODEL`, `<prefix>_API_KEY`,
/// `<prefix>_TIMEOUT_SECONDS`, `<prefix>_TEMPERATURE` and `<prefix>_MAX_TOKENS`. A variable
/// that is not set leaves the field as it is, which is what lets a caller layer the
/// environment under the command line: the flags are copied after this, and win.
/// @param config Configuration to fill, changed in place.
/// @param prefix Variable prefix, such as "OPENANIMA_CLI".
/// @param lookup Resolves a variable name to its value.
/// @throws OpenAnima::ConfigError when a variable is set to something that is not a
///         number inside its accepted range.
void applyApiSettings(OpenAnima::Provider::RestProviderConfig& config, const std::string& prefix,
                      const Lookup& lookup);

/// Copies the variables of one client from the process environment into a configuration.
/// @param config Configuration to fill, changed in place.
/// @param prefix Variable prefix, such as "OPENANIMA_CLI".
/// @throws OpenAnima::ConfigError when a variable is set to something that is not a
///         number inside its accepted range.
void applyApiSettings(OpenAnima::Provider::RestProviderConfig& config, const std::string& prefix);

/// Reads the API settings of one client from variables prefixed with its name.
///
/// The variables are `<prefix>_BASE_URL`, `<prefix>_MODEL`, `<prefix>_API_KEY`,
/// `<prefix>_TIMEOUT_SECONDS`, `<prefix>_TEMPERATURE` and `<prefix>_MAX_TOKENS`. The first
/// two are required, the rest fall back to the defaults of the configuration.
///
/// The prefix is what keeps two clients of the same library from sharing a model by
/// accident: each reads what it is given and nothing else.
/// @param prefix Variable prefix, such as "OPENANIMA_CLI".
/// @param lookup Resolves a variable name to its value.
/// @return The settings, ready to be handed to makeNpc().
/// @throws OpenAnima::ConfigError when a required variable is missing or a value is not a
///         number inside its accepted range.
OpenAnima::Provider::RestProviderConfig readApiSettings(const std::string& prefix,
                                                        const Lookup& lookup);

/// Reads the API settings of one client from the process environment.
/// @param prefix Variable prefix, such as "OPENANIMA_CLI".
/// @return The settings, ready to be handed to makeNpc().
/// @throws OpenAnima::ConfigError when a required variable is missing or a value is not a
///         number inside its accepted range.
OpenAnima::Provider::RestProviderConfig readApiSettings(const std::string& prefix);

}  // namespace OpenAnimaExample
