/// @file
/// @brief Provider for the OpenAI chat-completions contract.

#pragma once

#include <memory>
#include <vector>

#include "OpenAnima/Core/Completion.h"
#include "OpenAnima/Core/Types.h"
#include "OpenAnima/Http/HttpTransport.h"
#include "OpenAnima/Provider/IProvider.h"
#include "OpenAnima/Provider/ProviderConfig.h"

namespace OpenAnima::Provider {

/// Provider for the OpenAI chat-completions contract, which is also spoken by local
/// servers such as Ollama, llama.cpp and vLLM.
class RestProvider : public IProvider {
public:
    /// Creates a provider.
    /// @param config Endpoint, model and generation settings.
    /// @param transport HTTP boundary used for every call.
    RestProvider(ProviderConfig config, std::shared_ptr<OpenAnima::Http::HttpTransport> transport);

    /// @copydoc IProvider::complete
    OpenAnima::Core::Completion
    complete(const std::vector<OpenAnima::Core::ChatMessage>& messages) override;

    /// @return The configuration this provider was created with.
    const ProviderConfig& config() const noexcept {
        return m_config;
    }

private:
    ProviderConfig m_config;
    std::shared_ptr<OpenAnima::Http::HttpTransport> m_transport;
};

}  // namespace OpenAnima::Provider
