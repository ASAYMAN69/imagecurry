#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include "http_response.h"

#define CORS_HEADERS \
    "Access-Control-Allow-Origin: *\r\n" \
    "Access-Control-Allow-Methods: GET, POST, HEAD, OPTIONS\r\n" \
    "Access-Control-Allow-Headers: Content-Type, Content-Length, If-None-Match, If-Modified-Since, Authorization\r\n" \
    "Access-Control-Expose-Headers: Content-Length, Content-Type\r\n" \
    "Access-Control-Max-Age: 86400\r\n" \
    "Vary: Origin\r\n"

void send_response(int fd, int code, const char *status,
                   const char *content_type, const char *extra_headers,
                   const char *body, size_t body_len) {
    char header[2048];
    int len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        CORS_HEADERS
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "%s"
        "Connection: close\r\n"
        "\r\n",
        code, status, content_type, body_len,
        extra_headers ? extra_headers : "");

    send(fd, header, len, 0);
    if (body && body_len > 0) {
        send(fd, body, body_len, 0);
    }
}

void send_error(int fd, int code, const char *message) {
    const char *status;
    switch (code) {
        case 400: status = "Bad Request"; break;
        case 404: status = "Not Found"; break;
        case 413: status = "Payload Too Large"; break;
        case 500: status = "Internal Server Error"; break;
        case 501: status = "Not Implemented"; break;
        default: status = "Error"; break;
    }

    char body[256];
    size_t body_len = snprintf(body, sizeof(body),
        "<html><body><h1>%d %s</h1><p>%s</p></body></html>",
        code, status, message);

    send_response(fd, code, status, "text/html", NULL, body, body_len);
}

void send_not_modified(int fd, const char *etag, const char *last_modified) {
    char extra[256];
    snprintf(extra, sizeof(extra),
        "ETag: %s\r\n"
        "Last-Modified: %s\r\n"
        "Cache-Control: public, max-age=31536000, immutable\r\n",
        etag, last_modified);

    send_response(fd, 304, "Not Modified", "text/plain", extra, NULL, 0);
}
