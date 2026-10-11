#include "OpenAnima/OpenAnima.h"

#include <utility>

namespace OpenAnima {

Core::NPCBrain makeNpc(Provider::RestProviderConfig config,
                       std::shared_ptr<Http::HttpTransport> transport) {
    auto provider =
        std::make_shared<Provider::RestProvider>(std::move(config), std::move(transport));
    return Core::NPCBrain{std::move(provider)};
}

Core::NPCBrain makeNpc() {
    return makeNpc(Provider::RestProviderConfig::fromEnvironment());
}

Core::NPCBrain makeNpc(Provider::RestProviderConfig config) {
    return makeNpc(std::move(config), Http::CurlHttpTransport::create());
}

}  // namespace OpenAnima
