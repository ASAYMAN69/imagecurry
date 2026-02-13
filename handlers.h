#ifndef HANDLERS_H
#define HANDLERS_H

#include <stddef.h>

#define MAX_FILE_SIZE       (128 * 1024 * 1024)
#define BUFFER_SIZE         8192

void handle_options(int fd, const char *client_ip, int client_port);
void handle_get(int fd, const char *request, const char *filename,
                const char *client_ip, int client_port);
void handle_post(int fd, const char *request, const char *filename,
                 const char *body, size_t body_len,
                 const char *client_ip, int client_port);
void handle_head(int fd, const char *filename,
                 const char *client_ip, int client_port);

#endif
