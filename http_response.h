#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <stddef.h>

void send_response(int fd, int code, const char *status,
                   const char *content_type, const char *extra_headers,
                   const char *body, size_t body_len);
void send_error(int fd, int code, const char *message);
void send_not_modified(int fd, const char *etag, const char *last_modified);

#endif
