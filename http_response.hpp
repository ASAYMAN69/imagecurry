#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include <string>
#include <cstddef>

namespace ImageCurry {

void send_response(int fd, int code, const std::string& status,
                   const std::string& content_type,
                   const std::string& extra_headers,
                   const std::string& body);
void send_error(int fd, int code, const std::string& message);
void send_not_modified(int fd, const std::string& etag,
                       const std::string& last_modified);

}
#endif
