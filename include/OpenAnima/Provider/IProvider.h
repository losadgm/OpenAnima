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

/// One call to a provider: where the pieces go, and how to abandon it.
///
/// The two live together because a caller that paints an answer as it arrives is
/// also the caller that wants to be able to stop it half way, and because a second
/// callback in the argument list is a pair easy to hand over in the wrong order.
struct Call {
    /// Called with every piece of the answer as it arrives, in order. An empty sink
    /// asks for the answer in one response.
    DeltaSink onDelta;

    /// Asks whether the call should still be going.
    ///
    /// Asked on the client library's own timer, so it fires even while no data
    /// arrives: without it a caller could not abandon a stream that went quiet. An
    /// empty check never abandons the call.
    /// @return false to abandon the call.
    std::function<bool()> keepGoing;
};

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
    /// @param call Where the pieces go, and how the call can be abandoned.
    /// @return The assistant answer, its metadata, and why generation stopped.
    /// @throws OpenAnimaError subclasses when the call cannot be completed.
    virtual OpenAnima::Core::Completion
    complete(const std::vector<OpenAnima::Core::ChatMessage>& messages, const Call& call) = 0;

    /// Sends a whole conversation and reports the answer while it is produced.
    /// @param messages Conversation in chronological order; the last entry is the current turn.
    /// @param onDelta Called with every piece of the answer as it arrives, in order.
    /// @return The assembled answer, its metadata, and why generation stopped.
    /// @throws OpenAnimaError subclasses when the call cannot be completed.
    OpenAnima::Core::Completion complete(const std::vector<OpenAnima::Core::ChatMessage>& messages,
                                         const DeltaSink& onDelta) {
        return complete(messages, Call{onDelta, {}});
    }

    /// Sends a whole conversation and waits for the answer.
    /// @param messages Conversation in chronological order; the last entry is the current turn.
    /// @return The assistant answer, its metadata, and why generation stopped.
    /// @throws OpenAnimaError subclasses when the call cannot be completed.
    ///
    /// A class that implements the form above must repeat `using IProvider::complete;` to
    /// keep the conveniences reachable through its own type.
    OpenAnima::Core::Completion
    complete(const std::vector<OpenAnima::Core::ChatMessage>& messages) {
        return complete(messages, Call{});
    }

protected:
    /// Only derived classes build a provider.
    IProvider() = default;
};

}  // namespace OpenAnima::Provider
