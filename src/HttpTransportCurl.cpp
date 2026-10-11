#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

#include "OpenAnima/CurlHttpTransport.h"
#include "OpenAnima/Errors.h"

namespace OpenAnima {

namespace {

constexpr long ConnectTimeoutSeconds = 10;

/// Passes a value to libcurl, which takes every option through a variadic argument.
template <typename Value>
void setOption(CURL* handle, CURLoption option, Value value) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    curl_easy_setopt(handle, option, value);
}

/// libcurl wants one global initialisation before any easy handle exists.
void ensureCurlIsInitialized() {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw TransportError("libcurl could not be initialised");
        }
        std::atexit(curl_global_cleanup);
    });
}

size_t receiveBody(char* data, size_t size, size_t count, void* userdata) {
    auto* buffer = static_cast<std::string*>(userdata);
    buffer->append(data, size * count);
    return size * count;
}

}  // namespace

HttpResponse CurlHttpTransport::post(const HttpRequest& request) {
    assert(!request.url.empty() && "post() requires a URL");

    ensureCurlIsInitialized();

    CURL* handle = curl_easy_init();
    if (handle == nullptr) {
        throw TransportError("libcurl could not create a handle");
    }

    curl_slist* headers = nullptr;
    for (const HttpHeader& header : request.headers) {
        headers = curl_slist_append(headers, (header.first + ": " + header.second).c_str());
    }

    std::string body;
    setOption(handle, CURLOPT_URL, request.url.c_str());
    setOption(handle, CURLOPT_POST, 1L);
    setOption(handle, CURLOPT_POSTFIELDS, request.body.c_str());
    setOption(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    setOption(handle, CURLOPT_HTTPHEADER, headers);
    setOption(handle, CURLOPT_WRITEFUNCTION, receiveBody);
    setOption(handle, CURLOPT_WRITEDATA, &body);
    setOption(handle, CURLOPT_TIMEOUT, request.timeoutSeconds);
    setOption(handle, CURLOPT_CONNECTTIMEOUT, ConnectTimeoutSeconds);
    setOption(handle, CURLOPT_FOLLOWLOCATION, 1L);
    setOption(handle, CURLOPT_NOSIGNAL, 1L);
    setOption(handle, CURLOPT_USERAGENT, "OpenAnima");
    // CURLOPT_FAILONERROR stays off on purpose: an HTTP error is data for the provider
    // to interpret, not a transport failure.

    const CURLcode result = curl_easy_perform(handle);

    long statusCode = 0;
    if (result == CURLE_OK) {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &statusCode);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);

    if (result != CURLE_OK) {
        if (result == CURLE_OPERATION_TIMEDOUT) {
            throw TransportError("request to " + request.url + " timed out after " +
                                 std::to_string(request.timeoutSeconds) + "s");
        }
        throw TransportError("request to " + request.url +
                             " failed: " + curl_easy_strerror(result));
    }

    return HttpResponse{statusCode, std::move(body)};
}

std::shared_ptr<HttpTransport> CurlHttpTransport::create() {
    return std::make_shared<CurlHttpTransport>();
}

}  // namespace OpenAnima
