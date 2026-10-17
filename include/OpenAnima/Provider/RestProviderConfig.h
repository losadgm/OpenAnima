/// @file
/// @brief Provider settings.

#pragma once

#include <cstdint>
#include <string>

namespace OpenAnima::Provider {

/// Settings of a chat-completions provider.
///
/// The values come from the code that builds the provider. The library never reads the
/// environment: where a setting lives is a decision of the host application, and a library
/// that reaches for the process environment cannot be embedded twice in one process with
/// two different configurations.
///
/// The base URL points at the API root, with or without a trailing slash:
/// "http://localhost:11434/v1" for a local Ollama server, "https://api.openai.com/v1"
/// for a hosted service. Anything speaking the same contract works unchanged.
struct RestProviderConfig {
    static constexpr long MinTimeoutSeconds = 1;     ///< Shortest silence that can be allowed.
    static constexpr long MaxTimeoutSeconds = 3600;  ///< Longest silence that can be allowed.
    static constexpr double MinTemperature = 0.0;    ///< Lowest accepted temperature.
    static constexpr double MaxTemperature = 2.0;    ///< Highest accepted temperature.
    static constexpr std::int32_t MinMaxTokens = 1;  ///< Fewest tokens an answer can be bounded to.
    static constexpr std::int32_t MaxMaxTokens = 200000;  ///< Most tokens an answer can ask for.

    std::string baseUrl;          ///< API root, for example "http://localhost:11434/v1".
    std::string model;            ///< Model name sent with every request.
    std::string apiKey;           ///< Bearer token. Left empty for servers without authentication.
    long timeoutSeconds{60};      ///< Longest silence allowed, in seconds.
    double temperature{0.7};      ///< Sampling temperature.
    std::int32_t maxTokens{512};  ///< Upper bound on generated tokens.

    /// Checks that the settings can be used.
    /// A missing base URL or model is a broken precondition; a number out of its range is
    /// something the host can report and correct.
    /// @throws ConfigError when a numeric setting falls outside its accepted range.
    void validate() const;

    /// @return The chat-completions endpoint built from baseUrl, without a doubled slash.
    [[nodiscard]] std::string chatCompletionsUrl() const;
};

}  // namespace OpenAnima::Provider
