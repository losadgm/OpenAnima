/// @file
/// @brief Provider settings, and how they are read from the environment.

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace OpenAnima::Provider {

/// Settings of a chat-completions provider.
///
/// The base URL points at the API root, with or without a trailing slash:
/// "http://localhost:11434/v1" for a local Ollama server, "https://api.openai.com/v1"
/// for a hosted service. Anything speaking the same contract works unchanged.
struct ProviderConfig {
    /// Resolves an environment variable name to its value.
    /// Implementations must return an empty string when the variable is not set.
    using EnvironmentLookup = std::function<std::string(const char*)>;

    std::string baseUrl;          ///< API root, for example "http://localhost:11434/v1".
    std::string model;            ///< Model name sent with every request.
    std::string apiKey;           ///< Bearer token. Left empty for servers without authentication.
    long timeoutSeconds{60};      ///< Per-request budget in seconds.
    double temperature{0.7};      ///< Sampling temperature, between 0 and 2.
    std::int32_t maxTokens{512};  ///< Upper bound on generated tokens.

    /// Reads the configuration from the process environment.
    /// Required: OPENANIMA_BASE_URL, OPENANIMA_MODEL.
    /// Optional: OPENANIMA_API_KEY, OPENANIMA_TIMEOUT_SECONDS, OPENANIMA_TEMPERATURE,
    /// OPENANIMA_MAX_TOKENS.
    /// @return A configuration with every field resolved.
    /// @throws ConfigError when a required variable is missing or a value is malformed.
    static ProviderConfig fromEnvironment();

    /// Reads the configuration from a caller-supplied source of values.
    /// @param lookup Resolves a variable name to its value, empty when unset.
    /// @return A configuration with every field resolved.
    /// @throws ConfigError when a required variable is missing or a value is malformed.
    static ProviderConfig fromEnvironment(const EnvironmentLookup& lookup);

    /// @return The chat-completions endpoint built from baseUrl, without a doubled slash.
    std::string chatCompletionsUrl() const;
};

}  // namespace OpenAnima::Provider
