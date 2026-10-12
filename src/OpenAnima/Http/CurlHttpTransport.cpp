#include "OpenAnima/Http/CurlHttpTransport.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

#include "OpenAnima/Errors.h"

namespace OpenAnima::Http {

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

/// What the libcurl callbacks share between two invocations.
struct Exchange {
    const HttpRequest* request{nullptr};             ///< Carries the caller's keepGoing hook.
    const ResponseObserver* observer{nullptr};       ///< Where the answer goes.
    long status{0};                                  ///< Last status line that counts.
    std::chrono::steady_clock::time_point lastByte;  ///< When the body last moved.
    std::chrono::seconds silentFor{0};               ///< Longest silence allowed.
    bool stopped{false};                             ///< The caller asked to stop.
    bool wentQuiet{false};                           ///< Dropped for silence.
};

/// @param line A status line such as "HTTP/1.1 200 OK".
/// @return The status, or 0 when the line carries none.
long readStatus(std::string_view line) {
    const std::size_t space = line.find(' ');
    if (space == std::string_view::npos) {
        return 0;
    }
    long status = 0;
    for (std::size_t index = space + 1; index < line.size(); ++index) {
        const char digit = line[index];
        if (digit < '0' || digit > '9') {
            break;
        }
        status = status * 10 + (digit - '0');
    }
    return status;
}

/// Reads the status before the body starts, which is the only moment at which a
/// caller can still tell a stream from the body of a rejected call.
size_t receiveHeader(char* data, size_t size, size_t count, void* userdata) {
    const std::size_t length = size * count;
    auto* exchange = static_cast<Exchange*>(userdata);
    const std::string_view line(data, length);
    if (line.rfind("HTTP/", 0) != 0) {
        return length;
    }

    const long status = readStatus(line);
    if (status >= 300 && status < 400) {
        // A redirect: the status that counts is the one that follows it.
        return length;
    }
    exchange->status = status;
    if (exchange->observer->onStatus) {
        exchange->observer->onStatus(status);
    }
    return length;
}

size_t receiveBody(char* data, size_t size, size_t count, void* userdata) {
    const std::size_t length = size * count;
    auto* exchange = static_cast<Exchange*>(userdata);
    exchange->lastByte = std::chrono::steady_clock::now();

    if (exchange->observer->onBody && !exchange->observer->onBody(std::string_view(data, length))) {
        // A short write stops the transfer, which is how a caller cancels it.
        exchange->stopped = true;
        return 0;
    }
    return length;
}

/// Runs on libcurl's own timer, at least once a second, which is what makes a
/// cancellation land even while the stream is silent and no body has arrived.
int reportProgress(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* exchange = static_cast<Exchange*>(userdata);

    if (exchange->request->keepGoing && !exchange->request->keepGoing()) {
        exchange->stopped = true;
        return 1;
    }
    if (exchange->silentFor.count() > 0 &&
        std::chrono::steady_clock::now() - exchange->lastByte > exchange->silentFor) {
        exchange->wentQuiet = true;
        return 1;
    }
    return 0;
}

}  // namespace

long CurlHttpTransport::post(const HttpRequest& request, const ResponseObserver& observer) {
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

    Exchange exchange;
    exchange.request = &request;
    exchange.observer = &observer;
    exchange.silentFor = std::chrono::seconds(request.timeoutSeconds);
    exchange.lastByte = std::chrono::steady_clock::now();

    setOption(handle, CURLOPT_URL, request.url.c_str());
    setOption(handle, CURLOPT_POST, 1L);
    setOption(handle, CURLOPT_POSTFIELDS, request.body.c_str());
    setOption(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    setOption(handle, CURLOPT_HTTPHEADER, headers);
    setOption(handle, CURLOPT_HEADERFUNCTION, receiveHeader);
    setOption(handle, CURLOPT_HEADERDATA, &exchange);
    setOption(handle, CURLOPT_WRITEFUNCTION, receiveBody);
    setOption(handle, CURLOPT_WRITEDATA, &exchange);
    setOption(handle, CURLOPT_NOPROGRESS, 0L);
    setOption(handle, CURLOPT_XFERINFOFUNCTION, reportProgress);
    setOption(handle, CURLOPT_XFERINFODATA, &exchange);
    setOption(handle, CURLOPT_CONNECTTIMEOUT, ConnectTimeoutSeconds);
    setOption(handle, CURLOPT_FOLLOWLOCATION, 1L);
    setOption(handle, CURLOPT_NOSIGNAL, 1L);
    setOption(handle, CURLOPT_USERAGENT, "OpenAnima");
    // CURLOPT_TIMEOUT is deliberately absent: it bounds the whole transfer, and a
    // stream is allowed to last as long as it keeps producing. What has to be
    // bounded is the silence, which reportProgress() does.
    // CURLOPT_FAILONERROR stays off on purpose: an HTTP error is data for the
    // provider to interpret, not a transport failure.

    const CURLcode result = curl_easy_perform(handle);

    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);

    if (result != CURLE_OK && !exchange.stopped) {
        if (exchange.wentQuiet) {
            throw TransportError("no data from " + request.url + " for " +
                                 std::to_string(request.timeoutSeconds) + "s");
        }
        throw TransportError("request to " + request.url +
                             " failed: " + curl_easy_strerror(result));
    }

    return exchange.status;
}

std::shared_ptr<HttpTransport> CurlHttpTransport::create() {
    return std::make_shared<CurlHttpTransport>();
}

}  // namespace OpenAnima::Http
