#include "OpenAnima/OpenAnima.h"

#include <utility>

namespace OpenAnima {

Core::NPCBrain makeNpc(Provider::ProviderConfig config,
                       std::shared_ptr<Http::HttpTransport> transport) {
    auto provider =
        std::make_shared<Provider::RestProvider>(std::move(config), std::move(transport));
    return Core::NPCBrain{std::move(provider)};
}

#if defined(OPENANIMA_HAS_CURL)
Core::NPCBrain makeNpc() {
    return makeNpc(Provider::ProviderConfig::fromEnvironment());
}

Core::NPCBrain makeNpc(Provider::ProviderConfig config) {
    return makeNpc(std::move(config), Http::CurlHttpTransport::create());
}
#endif

}  // namespace OpenAnima
