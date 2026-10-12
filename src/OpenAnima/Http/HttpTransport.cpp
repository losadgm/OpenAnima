#include "OpenAnima/Http/HttpTransport.h"

#include <string>

namespace OpenAnima::Http {

ResponseObserver ResponseObserver::buffering(std::string& body) {
    ResponseObserver observer;
    observer.onBody = [&body](std::string_view piece) {
        body.append(piece);
        return true;
    };
    return observer;
}

HttpResponse HttpTransport::post(const HttpRequest& request) {
    HttpResponse response;
    response.statusCode = post(request, ResponseObserver::buffering(response.body));
    return response;
}

}  // namespace OpenAnima::Http
