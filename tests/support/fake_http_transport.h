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

/// In-memory transport: records every request and replays a scripted response, so
/// provider behaviour can be tested without opening a socket.
class FakeHttpTransport : public HttpTransport {
public:
    HttpResponse post(const HttpRequest& request) override {
        requests.push_back(request);
        if (failure != nullptr) {
            throw TransportError(failure);
        }
        return response;
    }

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
    const char* failure{nullptr};       ///< When set, post() throws with this message.
};

}  // namespace OpenAnima::TestSupport
