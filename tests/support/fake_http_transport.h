#pragma once

#include <string>
#include <vector>

#include "OpenAnima/Errors.h"
#include "OpenAnima/Http/HttpTransport.h"

namespace OpenAnima::TestSupport {

using OpenAnima::Http::HttpHeader;
using OpenAnima::Http::HttpRequest;
using OpenAnima::Http::HttpResponse;
using OpenAnima::Http::HttpTransport;
using OpenAnima::Http::ResponseObserver;

/// In-memory transport: records every request and replays a scripted response, so
/// provider behaviour can be tested without opening a socket.
class FakeHttpTransport : public HttpTransport {
public:
    long post(const HttpRequest& request, const ResponseObserver& observer) override {
        requests.push_back(request);
        if (failure != nullptr) {
            throw TransportError(failure);
        }
        if (observer.onStatus) {
            observer.onStatus(response.statusCode);
        }

        // The body can be scripted as several pieces, which is how a real socket
        // delivers it and the case a parser is most likely to get wrong.
        if (chunks.empty()) {
            chunks.push_back(response.body);
        }
        for (const std::string& chunk : chunks) {
            if (observer.onBody && !observer.onBody(chunk)) {
                break;
            }
        }
        return response.statusCode;
    }

    /// Keeps the buffered overload of the interface reachable through this type.
    using HttpTransport::post;

    /// @return The body of the last request, empty when nothing was sent.
    std::string lastBody() const {
        return requests.empty() ? std::string{} : requests.back().body;
    }

    /// @param name Header name to look for in the last request.
    /// @return The header value, empty when the header was not sent.
    std::string lastHeader(const std::string& name) const {
        if (requests.empty()) {
            return {};
        }
        for (const HttpHeader& header : requests.back().headers) {
            if (header.first == name) {
                return header.second;
            }
        }
        return {};
    }

    std::vector<HttpRequest> requests;  ///< Every request received, in order.
    HttpResponse response{200, "{}"};   ///< Response replayed for every call.
    std::vector<std::string> chunks;    ///< Pieces the body arrives in; empty means one piece.
    const char* failure{nullptr};       ///< When set, post() throws with this message.
};

}  // namespace OpenAnima::TestSupport
