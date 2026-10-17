#include "common/apiSettings.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>

#include "OpenAnima/Errors.h"

namespace OpenAnimaExample {
namespace {

using OpenAnima::ConfigError;
using OpenAnima::Provider::RestProviderConfig;

/// One numeric setting: the variable it lives in, its fallback and what it accepts.
template <typename T>
struct Setting {
    std::string name;
    T fallback;
    T minimum;
    T maximum;
};

/// @return The value of a mandatory variable.
/// @throws ConfigError when it is not set or blank.
std::string requiredValue(const Lookup& lookup, const std::string& name) {
    const std::string value = lookup(name.c_str());
    if (value.empty()) {
        throw ConfigError(name + " is not set");
    }
    return value;
}

/// @return The value of an integer variable, or its fallback when it is not set.
/// @throws ConfigError when it is not an integer or falls outside its range.
long integerValue(const Lookup& lookup, const Setting<long>& setting) {
    const std::string raw = lookup(setting.name.c_str());
    if (raw.empty()) {
        return setting.fallback;
    }

    std::size_t consumed = 0;
    long value = 0;
    try {
        value = std::stol(raw, &consumed);
    } catch (const std::exception&) {
        throw ConfigError(setting.name + " is not an integer: '" + raw + "'");
    }
    if (consumed != raw.size() || value < setting.minimum || value > setting.maximum) {
        throw ConfigError(setting.name + " must be an integer between " +
                          std::to_string(setting.minimum) + " and " +
                          std::to_string(setting.maximum) + ", got '" + raw + "'");
    }
    return value;
}

/// @return The value of a number variable, or its fallback when it is not set.
/// @throws ConfigError when it is not a finite number or falls outside its range.
double numberValue(const Lookup& lookup, const Setting<double>& setting) {
    const std::string raw = lookup(setting.name.c_str());
    if (raw.empty()) {
        return setting.fallback;
    }

    std::size_t consumed = 0;
    double value = 0.0;
    try {
        value = std::stod(raw, &consumed);
    } catch (const std::exception&) {
        throw ConfigError(setting.name + " is not a number: '" + raw + "'");
    }
    if (consumed != raw.size() || !std::isfinite(value) || value < setting.minimum ||
        value > setting.maximum) {
        throw ConfigError(setting.name + " must be a number between " +
                          std::to_string(setting.minimum) + " and " +
                          std::to_string(setting.maximum) + ", got '" + raw + "'");
    }
    return value;
}

}  // namespace

RestProviderConfig readApiSettings(const std::string& prefix, const Lookup& lookup) {
    assert(!prefix.empty() && "a client needs a variable prefix");
    assert(lookup != nullptr);

    RestProviderConfig config;
    config.baseUrl = requiredValue(lookup, prefix + "_BASE_URL");
    config.model = requiredValue(lookup, prefix + "_MODEL");
    config.apiKey = lookup((prefix + "_API_KEY").c_str());
    config.timeoutSeconds = integerValue(
        lookup, {prefix + "_TIMEOUT_SECONDS", config.timeoutSeconds,
                 RestProviderConfig::MinTimeoutSeconds, RestProviderConfig::MaxTimeoutSeconds});
    config.temperature = numberValue(lookup, {prefix + "_TEMPERATURE", config.temperature,
                                              RestProviderConfig::MinTemperature,
                                              RestProviderConfig::MaxTemperature});
    config.maxTokens = static_cast<std::int32_t>(
        integerValue(lookup, {prefix + "_MAX_TOKENS", config.maxTokens,
                              RestProviderConfig::MinMaxTokens, RestProviderConfig::MaxMaxTokens}));
    return config;
}

RestProviderConfig readApiSettings(const std::string& prefix) {
    return readApiSettings(prefix, [](const char* name) -> std::string {
        const char* value = std::getenv(name);
        return value == nullptr ? std::string{} : std::string{value};
    });
}

}  // namespace OpenAnimaExample
