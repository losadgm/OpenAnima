#pragma once

#include <string>
#include <utility>
#include <vector>

namespace OpenAnima::Http {

/// A single HTTP header. A vector of pairs is used so repeated names survive.
using HttpHeader = std::pair<std::string, std::string>;

/// A request issued by an inference provider.
struct HttpRequest {
    std::string url;                  ///< Absolute endpoint, scheme included.
    std::vector<HttpHeader> headers;  ///< Headers sent verbatim.
    std::string body;                 ///< Raw payload, usually JSON.
    long timeoutSeconds{60};          ///< Wall-clock budget for the whole exchange.
};

/// The raw answer of an endpoint, kept verbatim so callers can inspect the status.
struct HttpResponse {
    long statusCode{0};  ///< HTTP status, or 0 when the transport could not report one.
    std::string body;    ///< Response body exactly as received.
};

/// The HTTP boundary of the library.
/// Providers depend on this interface and never on a concrete client library, which
/// keeps them testable with an in-memory implementation and keeps the socket code
/// in a single place.
class HttpTransport {
public:
    virtual ~HttpTransport() = default;

    /// Transports are used through a pointer, so copying and moving are not offered.
    HttpTransport(const HttpTransport&) = delete;
    HttpTransport& operator=(const HttpTransport&) = delete;
    HttpTransport(HttpTransport&&) = delete;
    HttpTransport& operator=(HttpTransport&&) = delete;

    /// Performs a blocking POST.
    /// A non-2xx status is not a failure here: it is reported through the response so
    /// the caller decides what it means.
    /// @param request Fully built request.
    /// @return The raw response.
    /// @throws TransportError when the exchange cannot be completed at all.
    virtual HttpResponse post(const HttpRequest& request) = 0;

protected:
    /// Only derived classes build a transport.
    HttpTransport() = default;
};

}  // namespace OpenAnima::Http
