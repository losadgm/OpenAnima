#pragma once

#include <memory>
#include <string>
#include <vector>

#include "OpenAnima/HttpTransport.h"
#include "OpenAnima/IProvider.h"
#include "OpenAnima/ProviderConfig.h"
#include "OpenAnima/Types.h"

namespace OpenAnima {

/// Provider for the OpenAI chat-completions contract, which is also spoken by local
/// servers such as Ollama, llama.cpp and vLLM.
class RestProvider : public IProvider {
public:
    /// Creates a provider.
    /// @param config Endpoint, model and generation settings.
    /// @param transport HTTP boundary used for every call.
    RestProvider(ProviderConfig config, std::shared_ptr<HttpTransport> transport);

    /// @copydoc IProvider::complete
    std::string complete(const std::vector<ChatMessage>& messages) override;

    /// @return The configuration this provider was created with.
    const ProviderConfig& config() const noexcept {
        return m_config;
    }

private:
    ProviderConfig m_config;
    std::shared_ptr<HttpTransport> m_transport;
};

}  // namespace OpenAnima
