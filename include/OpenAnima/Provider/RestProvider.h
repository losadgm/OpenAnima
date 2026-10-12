/// @file
/// @brief Provider for the OpenAI chat-completions contract.

#pragma once

#include <memory>
#include <vector>

#include "OpenAnima/Core/Completion.h"
#include "OpenAnima/Core/Types.h"
#include "OpenAnima/Http/HttpTransport.h"
#include "OpenAnima/Provider/IProvider.h"
#include "OpenAnima/Provider/RestProviderConfig.h"

namespace OpenAnima::Provider {

/// Provider for the OpenAI chat-completions contract, which is also spoken by local
/// servers such as Ollama, llama.cpp and vLLM.
class RestProvider : public IProvider {
public:
    /// Creates a provider.
    /// @param config Endpoint, model and generation settings.
    /// @param transport HTTP boundary used for every call.
    RestProvider(RestProviderConfig config,
                 std::shared_ptr<OpenAnima::Http::HttpTransport> transport);

    /// Sends a whole conversation and returns the answer.
    ///
    /// The same contract as the interface, written out again because the interface has
    /// three `complete` members and a `@copydoc` cannot say which one counts.
    /// @param messages Conversation in chronological order; the last entry is the current turn.
    /// @param call Where the pieces go, and how the call can be abandoned.
    /// @return The assistant answer, its metadata, and why generation stopped.
    /// @throws OpenAnimaError subclasses when the call cannot be completed.
    OpenAnima::Core::Completion complete(const std::vector<OpenAnima::Core::ChatMessage>& messages,
                                         const Call& call) override;

    /// Sends a whole conversation and reports the answer while it is produced.
    /// @param messages Conversation in chronological order; the last entry is the current turn.
    /// @param onDelta Called with every piece of the answer as it arrives, in order.
    /// @return The assistant answer, its metadata, and why generation stopped.
    /// @throws OpenAnimaError subclasses when the call cannot be completed.
    OpenAnima::Core::Completion complete(const std::vector<OpenAnima::Core::ChatMessage>& messages,
                                         const DeltaSink& onDelta) {
        return IProvider::complete(messages, onDelta);
    }

    /// Sends a whole conversation and waits for the answer.
    /// @param messages Conversation in chronological order; the last entry is the current turn.
    /// @return The assistant answer, its metadata, and why generation stopped.
    /// @throws OpenAnimaError subclasses when the call cannot be completed.
    OpenAnima::Core::Completion
    complete(const std::vector<OpenAnima::Core::ChatMessage>& messages) {
        return IProvider::complete(messages);
    }

    /// @return The configuration this provider was created with.
    [[nodiscard]] const RestProviderConfig& config() const noexcept {
        return m_config;
    }

private:
    /// Sends the request and builds the answer out of the chunks of the stream.
    /// @param request Fully built streamed request.
    /// @param call Where the pieces go, and how the call can be abandoned.
    /// @return The assembled answer.
    OpenAnima::Core::Completion readStream(const OpenAnima::Http::HttpRequest& request,
                                           const Call& call);

    RestProviderConfig m_config;
    std::shared_ptr<OpenAnima::Http::HttpTransport> m_transport;
};

}  // namespace OpenAnima::Provider
