#pragma once

#include <string>
#include <vector>

#include "OpenAnima/Core/Types.h"

namespace OpenAnima::Provider {

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

    /// Sends a whole conversation and returns the assistant reply.
    /// @param messages Conversation in chronological order; the last entry is the current turn.
    /// @return The assistant text produced by the backend.
    /// @throws OpenAnimaError subclasses when the call cannot be completed.
    virtual std::string complete(const std::vector<OpenAnima::Core::ChatMessage>& messages) = 0;

protected:
    /// Only derived classes build a provider.
    IProvider() = default;
};

}  // namespace OpenAnima::Provider
