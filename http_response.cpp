#include "http_response.hpp"
#include <unistd.h>
#include <sys/socket.h>

namespace ImageCurry {

constexpr const char* CORS_HEADERS =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, HEAD, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type, Content-Length, If-None-Match, If-Modified-Since, Authorization\r\n"
    "Access-Control-Expose-Headers: Content-Length, Content-Type\r\n"
    "Access-Control-Max-Age: 86400\r\n"
    "Vary: Origin\r\n";

void send_response(int fd, int code, const std::string& status,
                   const std::string& content_type,
                   const std::string& extra_headers,
                   const std::string& body) {
    std::string header = "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n";
    header += CORS_HEADERS;
    header += "Content-Type: " + content_type + "\r\n";
    header += "Content-Length: " + std::to_string(body.size()) + "\r\n";

    if (!extra_headers.empty()) {
        header += extra_headers + "\r\n";
    }

    header += "Connection: close\r\n\r\n";

    send(fd, header.data(), header.size(), 0);

    if (!body.empty()) {
        send(fd, body.data(), body.size(), 0);
    }
}

void send_error(int fd, int code, const std::string& message) {
    std::string status;
    switch (code) {
        case 400: status = "Bad Request"; break;
        case 404: status = "Not Found"; break;
        case 413: status = "Payload Too Large"; break;
        case 500: status = "Internal Server Error"; break;
        case 501: status = "Not Implemented"; break;
        default: status = "Error"; break;
    }

    std::string body = "<html><body><h1>" + std::to_string(code) + " " +
                       status + "</h1><p>" + message + "</p></body></html>";

    send_response(fd, code, status, "text/html", "", body);
}

void send_not_modified(int fd, const std::string& etag,
                       const std::string& last_modified) {
    std::string extra = "ETag: " + etag + "\r\n" +
                        "Last-Modified: " + last_modified + "\r\n" +
                        "Cache-Control: public, max-age=31536000, immutable";

    send_response(fd, 304, "Not Modified", "text/plain", extra, "");
}

}
