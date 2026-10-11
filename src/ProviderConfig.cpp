#include "OpenAnima/ProviderConfig.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <string>

#include "OpenAnima/Errors.h"

namespace OpenAnima {

namespace {

// Names of every recognised variable, spelled once.
constexpr const char* BaseUrlVariable = "OPENANIMA_BASE_URL";
constexpr const char* ModelVariable = "OPENANIMA_MODEL";
constexpr const char* ApiKeyVariable = "OPENANIMA_API_KEY";
constexpr const char* TimeoutVariable = "OPENANIMA_TIMEOUT_SECONDS";
constexpr const char* TemperatureVariable = "OPENANIMA_TEMPERATURE";
constexpr const char* MaxTokensVariable = "OPENANIMA_MAX_TOKENS";

/// Inclusive range accepted for a numeric setting.
template <typename Value>
struct Bounds {
    Value minimum;
    Value maximum;
};

constexpr Bounds<long> TimeoutBounds{1, 3600};
constexpr Bounds<double> TemperatureBounds{0.0, 2.0};
constexpr Bounds<long> MaxTokensBounds{1, 200000};

/// Renders a bound the way a person would write it, without trailing zeros.
std::string formatBound(long value) {
    return std::to_string(value);
}

std::string formatBound(double value) {
    std::string text = std::to_string(value);
    text.erase(text.find_last_not_of('0') + 1, std::string::npos);
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

/// @return The value of a mandatory variable.
/// @throws ConfigError when the variable is not set or blank.
std::string requireValue(const ProviderConfig::EnvironmentLookup& lookup, const char* name) {
    const std::string value = lookup(name);
    if (value.empty()) {
        throw ConfigError(std::string(name) + " is not set");
    }
    return value;
}

/// @return The value of a numeric variable, or the fallback when it is not set.
/// @throws ConfigError when the value is not an integer inside the accepted range.
long parseLongValue(const ProviderConfig::EnvironmentLookup& lookup, const char* name,
                    long fallback, Bounds<long> bounds) {
    const std::string raw = lookup(name);
    if (raw.empty()) {
        return fallback;
    }

    std::size_t consumed = 0;
    long value = 0;
    try {
        value = std::stol(raw, &consumed);
    } catch (const std::exception&) {
        throw ConfigError(std::string(name) + " is not an integer: '" + raw + "'");
    }

    if (consumed != raw.size() || value < bounds.minimum || value > bounds.maximum) {
        throw ConfigError(std::string(name) + " must be an integer between " +
                          formatBound(bounds.minimum) + " and " + formatBound(bounds.maximum) +
                          ", got '" + raw + "'");
    }
    return value;
}

/// @return The value of a floating-point variable, or the fallback when it is not set.
/// @throws ConfigError when the value is not finite or falls outside the accepted range.
double parseDoubleValue(const ProviderConfig::EnvironmentLookup& lookup, const char* name,
                        double fallback, Bounds<double> bounds) {
    const std::string raw = lookup(name);
    if (raw.empty()) {
        return fallback;
    }

    std::size_t consumed = 0;
    double value = 0.0;
    try {
        value = std::stod(raw, &consumed);
    } catch (const std::exception&) {
        throw ConfigError(std::string(name) + " is not a number: '" + raw + "'");
    }

    if (consumed != raw.size() || !std::isfinite(value) || value < bounds.minimum ||
        value > bounds.maximum) {
        throw ConfigError(std::string(name) + " must be a number between " +
                          formatBound(bounds.minimum) + " and " + formatBound(bounds.maximum) +
                          ", got '" + raw + "'");
    }
    return value;
}

}  // namespace

ProviderConfig ProviderConfig::fromEnvironment() {
    return fromEnvironment([](const char* name) -> std::string {
        const char* value = std::getenv(name);
        return value == nullptr ? std::string{} : std::string{value};
    });
}

ProviderConfig ProviderConfig::fromEnvironment(const EnvironmentLookup& lookup) {
    // A missing lookup is a programming error, not a configuration problem.
    assert(lookup != nullptr);

    ProviderConfig config;
    config.baseUrl = requireValue(lookup, BaseUrlVariable);
    config.model = requireValue(lookup, ModelVariable);
    config.apiKey = lookup(ApiKeyVariable);
    config.timeoutSeconds =
        parseLongValue(lookup, TimeoutVariable, config.timeoutSeconds, TimeoutBounds);
    config.temperature =
        parseDoubleValue(lookup, TemperatureVariable, config.temperature, TemperatureBounds);
    config.maxTokens = static_cast<std::int32_t>(
        parseLongValue(lookup, MaxTokensVariable, config.maxTokens, MaxTokensBounds));
    return config;
}

std::string ProviderConfig::chatCompletionsUrl() const {
    assert(!baseUrl.empty());

    std::string url = baseUrl;
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url + "/chat/completions";
}

}  // namespace OpenAnima
