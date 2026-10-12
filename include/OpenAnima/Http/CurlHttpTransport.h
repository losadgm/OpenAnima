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

    /// Performs a blocking POST and reports the answer while it arrives.
    ///
    /// The same contract as the interface, written out again because the interface has
    /// two `post` members and a `@copydoc` cannot say which one counts.
    /// @param request Fully built request.
    /// @param observer Where the status and the body go, as they arrive.
    /// @return The status the endpoint answered with, or 0 when it reported none.
    /// @throws TransportError when the exchange cannot be completed at all.
    long post(const HttpRequest& request, const ResponseObserver& observer) override;

    /// Performs a blocking POST and returns the whole answer.
    /// @param request Fully built request.
    /// @return The status and the body, buffered.
    /// @throws TransportError when the exchange cannot be completed at all.
    HttpResponse post(const HttpRequest& request) {
        return HttpTransport::post(request);
    }

    /// @return A transport ready to be injected into a provider.
    static std::shared_ptr<HttpTransport> create();
};

}  // namespace OpenAnima::Http
