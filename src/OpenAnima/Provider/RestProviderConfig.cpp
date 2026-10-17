#include "OpenAnima/Provider/RestProviderConfig.h"

#include <cassert>
#include <cmath>
#include <string>

#include "OpenAnima/Errors.h"

namespace OpenAnima::Provider {
namespace {

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

}  // namespace

void RestProviderConfig::validate() const {
    assert(!baseUrl.empty() && "a provider needs a base URL");
    assert(!model.empty() && "a provider needs a model");

    if (timeoutSeconds < MinTimeoutSeconds || timeoutSeconds > MaxTimeoutSeconds) {
        throw ConfigError("the timeout must be between " + formatBound(MinTimeoutSeconds) +
                          " and " + formatBound(MaxTimeoutSeconds) + " seconds, got " +
                          std::to_string(timeoutSeconds));
    }
    if (!std::isfinite(temperature) || temperature < MinTemperature ||
        temperature > MaxTemperature) {
        throw ConfigError("the temperature must be between " + formatBound(MinTemperature) +
                          " and " + formatBound(MaxTemperature) + ", got " +
                          formatBound(temperature));
    }
    if (maxTokens < MinMaxTokens || maxTokens > MaxMaxTokens) {
        throw ConfigError("maxTokens must be between " + std::to_string(MinMaxTokens) + " and " +
                          std::to_string(MaxMaxTokens) + ", got " + std::to_string(maxTokens));
    }
}

std::string RestProviderConfig::chatCompletionsUrl() const {
    assert(!baseUrl.empty());

    std::string url = baseUrl;
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url + "/chat/completions";
}

}  // namespace OpenAnima::Provider
