#ifndef HANDLERS_H
#define HANDLERS_H

#include <string>

namespace ImageCurry {

constexpr size_t MAX_FILE_SIZE = 128 * 1024 * 1024;
constexpr int BUFFER_SIZE = 8192;

void handle_options(int fd, const std::string& client_ip, int client_port);
void handle_get(int fd, const std::string& request, const std::string& filename,
                const std::string& client_ip, int client_port);
void handle_post(int fd, const std::string& request, const std::string& filename,
                 const std::string& body, size_t body_len,
                 const std::string& client_ip, int client_port);
void handle_head(int fd, const std::string& filename,
                 const std::string& client_ip, int client_port);

}
#endif
