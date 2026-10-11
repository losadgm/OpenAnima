#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace OpenAnima::Core {

/// Why a provider stopped generating.
enum class FinishReason : std::uint8_t {
    Unknown,       ///< Not reported, or a value this library does not model.
    Stop,          ///< Natural end, or a stop sequence was reached.
    Length,        ///< The token budget ran out before the answer finished.
    ToolCalls,     ///< The model asked for a tool instead of answering.
    ContentFilter  ///< The provider withheld or trimmed the answer.
};

/// Tokens billed for one exchange.
struct TokenUsage {
    std::int32_t promptTokens{0};      ///< Tokens in the conversation that was sent.
    std::int32_t completionTokens{0};  ///< Tokens in the answer that came back.
    std::int32_t totalTokens{0};       ///< Sum reported by the provider.
};

/// One answer from a provider, together with what it cost and why it ended.
///
/// Providers are free to leave the metadata out: `usage` stays empty when the
/// provider reports none, and `finishReason` stays Unknown when the value is not
/// one of the modelled ones.
struct Completion {
    std::string text;                                  ///< Assistant text, ready to show.
    std::string model;                                 ///< Model that actually answered.
    FinishReason finishReason{FinishReason::Unknown};  ///< Why generation stopped.
    std::optional<TokenUsage> usage;                   ///< Empty when none was reported.
};

}  // namespace OpenAnima::Core
