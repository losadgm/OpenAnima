/// @file
/// @brief The HTTP boundary, small enough to be replaced in a test.

#pragma once

#include <functional>
#include <string>
#include <string_view>
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
    long timeoutSeconds{60};          ///< Longest gap with no bytes before giving up.

    /// Asks whether the exchange should still be going.
    ///
    /// Called on the client library's own timer, so it fires even while no data
    /// arrives: without it a caller could not abandon a stream that went quiet.
    /// Empty means the exchange is never abandoned.
    /// @return false to stop the exchange.
    std::function<bool()> keepGoing;
};

/// The raw answer of an endpoint, kept verbatim so callers can inspect the status.
struct HttpResponse {
    long statusCode{0};  ///< HTTP status, or 0 when the transport could not report one.
    std::string body;    ///< Response body exactly as received.
};

/// Watches a response while it is being received.
///
/// A buffered exchange only wants the body at the end. A streamed one, like the
/// `text/event-stream` a chat-completions call answers with, wants every piece as
/// it arrives, and wants the status before the first piece: the body of a rejected
/// call is not a stream at all, and reading it as one hides the real error.
///
/// Both callbacks may be left empty, and are then simply not called.
struct ResponseObserver {
    /// Called once the status is known and before any body arrives.
    std::function<void(long statusCode)> onStatus;

    /// Called for each piece of the body, in order.
    /// @param body Bytes just received; valid only for the duration of the call.
    /// @return false to stop the exchange.
    std::function<bool(std::string_view body)> onBody;

    /// @param body String every piece is appended to.
    /// @return An observer that buffers the body and ignores the status.
    static ResponseObserver buffering(std::string& body);
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

    /// Performs a blocking POST and reports the answer while it arrives.
    /// A non-2xx status is not a failure here: it is reported through the observer
    /// and returned, so the caller decides what it means.
    /// @param request Fully built request.
    /// @param observer Where the status and the body go.
    /// @return The status the endpoint answered with, or 0 when it reported none.
    /// @throws TransportError when the exchange cannot be completed at all.
    ///
    /// A class that implements this one must repeat `using HttpTransport::post;` to
    /// keep the buffered overload below reachable through its own type.
    virtual long post(const HttpRequest& request, const ResponseObserver& observer) = 0;

    /// Performs a blocking POST and returns the whole answer.
    /// @param request Fully built request.
    /// @return The status and the body, buffered.
    /// @throws TransportError when the exchange cannot be completed at all.
    HttpResponse post(const HttpRequest& request);

protected:
    /// Only derived classes build a transport.
    HttpTransport() = default;
};

}  // namespace OpenAnima::Http
