/// @file
/// @brief The inference backend interface.

#pragma once

#include <functional>
#include <string_view>
#include <vector>

#include "OpenAnima/Core/Completion.h"
#include "OpenAnima/Core/Types.h"

namespace OpenAnima::Provider {

/// Receives the answer as a provider produces it.
///
/// An empty sink means the caller wants the whole answer at once, and a provider is
/// then free to ask for it in a single response. That is not only an optimisation:
/// some servers report the token usage only when the answer is not streamed.
using DeltaSink = std::function<void(std::string_view text)>;

/// An interchangeable inference backend.
/// The rest of the library talks to this interface, so replacing a hosted API with a
/// local model is a configuration change rather than a code change.
class IProvider {
public:
    virtual ~IProvider() = default;

    /// Providers are used through a pointer, so copying and moving are not offered.
    IProvider(const IProvider&) = delete;
    IProvider& operator=(const IProvider&) = delete;
    IProvider(IProvider&&) = delete;
    IProvider& operator=(IProvider&&) = delete;

    /// Sends a whole conversation and returns the answer.
    /// @param messages Conversation in chronological order; the last entry is the current turn.
    /// @param onDelta Called with every piece of the answer as it arrives, in order. An
    ///        empty sink asks for the answer in one response, which is also the only
    ///        mode in which every server reports the token usage.
    /// @return The assistant answer, its metadata, and why generation stopped.
    /// @throws OpenAnimaError subclasses when the call cannot be completed.
    virtual OpenAnima::Core::Completion
    complete(const std::vector<OpenAnima::Core::ChatMessage>& messages,
             const DeltaSink& onDelta) = 0;

    /// Sends a whole conversation and waits for the answer.
    /// @param messages Conversation in chronological order; the last entry is the current turn.
    /// @return The assistant answer, its metadata, and why generation stopped.
    /// @throws OpenAnimaError subclasses when the call cannot be completed.
    ///
    /// A class that implements the form above must repeat `using IProvider::complete;` to
    /// keep this overload reachable through its own type.
    OpenAnima::Core::Completion
    complete(const std::vector<OpenAnima::Core::ChatMessage>& messages) {
        return complete(messages, DeltaSink{});
    }

protected:
    /// Only derived classes build a provider.
    IProvider() = default;
};

}  // namespace OpenAnima::Provider
