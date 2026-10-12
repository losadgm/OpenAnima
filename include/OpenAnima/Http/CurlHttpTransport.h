/// @file
/// @brief HTTP transport built on libcurl.

#pragma once

#include <memory>

#include "OpenAnima/Http/HttpTransport.h"

namespace OpenAnima::Http {

/// HTTP transport built on libcurl, the one client library that offers TLS on both
/// Windows and Linux without platform-specific code.
class CurlHttpTransport : public HttpTransport {
public:
    CurlHttpTransport() = default;

    /// @copydoc HttpTransport::post
    long post(const HttpRequest& request, const ResponseObserver& observer) override;

    /// Keeps the buffered overload of the interface reachable through this type.
    using HttpTransport::post;

    /// @return A transport ready to be injected into a provider.
    static std::shared_ptr<HttpTransport> create();
};

}  // namespace OpenAnima::Http
