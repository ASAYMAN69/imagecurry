#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "logging.h"
#include "http_response.h"
#include "handlers.h"
#include "utils.h"

#define SERVER_PORT         8080
#define MAX_CONNECTIONS     128
#define BUFFER_SIZE         8192
#define LOG_FILE            "./server.log"
#define REQUEST_TIMEOUT     30
#define MAX_REQUEST_SIZE    (128 * 1024 * 1024)

static volatile int server_running = 1;

void signal_handler(int signum) {
    (void)signum;
    server_running = 0;
}

int ensure_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (mkdir(path, 0755) != 0) {
            log_msg(LOG_ERROR, NULL, 0, NULL, NULL, 0,
                    "Failed to create directory %s: %s", path, strerror(errno));
            return 0;
        }
        log_msg(LOG_INFO, NULL, 0, NULL, NULL, 0,
                "Created directory: %s", path);
    } else if (!S_ISDIR(st.st_mode)) {
        log_msg(LOG_ERROR, NULL, 0, NULL, NULL, 0,
                "%s exists but is not a directory", path);
        return 0;
    }
    return 1;
}

void process_request(int client_fd, struct sockaddr_in *client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr->sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr->sin_port);

    struct timeval tv;
    tv.tv_sec = REQUEST_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char header_buf[BUFFER_SIZE];
    ssize_t received = 0;
    char *header_end = NULL;
    while(received < BUFFER_SIZE - 1) {
        ssize_t n = recv(client_fd, header_buf + received, BUFFER_SIZE - received - 1, 0);
        if (n <= 0) {
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                log_msg(LOG_ERROR, client_ip, client_port, NULL, NULL, 0,
                        "Receive error: %s", strerror(errno));
            }
            return;
        }
        received += n;
        header_buf[received] = '\0';
        header_end = strstr(header_buf, "\r\n\r\n");
        if(header_end) break;
    }

    if (!header_end) {
        log_msg(LOG_WARN, client_ip, client_port, "INVALID", NULL, 400,
                "Headers too large or malformed");
        send_error(client_fd, 400, "Headers too large or malformed");
        return;
    }

    char method[16] = {0};
    char path[512] = {0};
    char version[16] = {0};

    if (sscanf(header_buf, "%15s %511s %15s", method, path, version) != 3) {
        log_msg(LOG_WARN, client_ip, client_port, "INVALID", path, 400,
                "Malformed request");
        send_error(client_fd, 400, "Malformed request");
        return;
    }

    if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) {
        log_msg(LOG_WARN, client_ip, client_port, method, path, 400,
                "Invalid HTTP version: %s", version);
        send_error(client_fd, 400, "Invalid HTTP version");
        return;
    }

    if (strcmp(method, "OPTIONS") == 0) {
        handle_options(client_fd, client_ip, client_port);
        return;
    }

    char *query = strchr(path, '?');
    size_t path_len = query ? (size_t)(query - path) : strlen(path);

    if (path_len != 1 || path[0] != '/') {
        log_msg(LOG_WARN, client_ip, client_port, method, path, 400,
                "Invalid path");
        send_error(client_fd, 400, "Invalid path - only / is supported");
        return;
    }

    char filename[MAX_FILENAME_LEN + 1] = {0};
    if (!query || !get_query_param(query + 1, "name", filename, sizeof(filename))) {
        log_msg(LOG_WARN, client_ip, client_port, method, path, 400,
                "Missing 'name' parameter");
        send_error(client_fd, 400, "Missing 'name' parameter");
        return;
    }

    if (!valid_filename(filename)) {
        log_msg(LOG_WARN, client_ip, client_port, method, path, 400,
                "Invalid filename: %s", filename);
        send_error(client_fd, 400, "Invalid filename");
        return;
    }

    char *body = NULL;
    size_t body_len = 0;

    char *content_len_str = strstr(header_buf, "Content-Length:");
    if (content_len_str) {
        size_t content_length = atol(content_len_str + 15);
        if (content_length > MAX_REQUEST_SIZE) {
            send_error(client_fd, 413, "Payload Too Large");
            return;
        }

        if (content_length > 0) {
            body = malloc(content_length + 1);
            if (!body) {
                send_error(client_fd, 500, "Internal server error");
                return;
            }

            size_t header_len = header_end - header_buf + 4;
            ssize_t body_part_len = received - header_len;

            if (body_part_len > 0) {
                memcpy(body, header_end + 4, body_part_len);
            }
            body_len = (body_part_len > 0) ? body_part_len : 0;

            while(body_len < content_length) {
                ssize_t n = recv(client_fd, body + body_len, content_length - body_len, 0);
                if (n <= 0) {
                     if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        log_msg(LOG_ERROR, client_ip, client_port, NULL, NULL, 0,
                                "Receive error: %s", strerror(errno));
                    }
                    free(body);
                    return;
                }
                body_len += n;
            }
            body[body_len] = '\0';
        }
    }

    if (strcmp(method, "GET") == 0) {
        handle_get(client_fd, header_buf, filename, client_ip, client_port);
    } else if (strcmp(method, "POST") == 0) {
        handle_post(client_fd, header_buf, filename, body, body_len,
                    client_ip, client_port);
    } else if (strcmp(method, "HEAD") == 0) {
        handle_head(client_fd, filename, client_ip, client_port);
    } else {
        log_msg(LOG_WARN, client_ip, client_port, method, path, 501,
                "Method not implemented");
        send_error(client_fd, 501, "Method not implemented");
    }

    if (body) free(body);
}

int main(void) {
    log_init(LOG_FILE);
    log_msg(LOG_INFO, NULL, 0, NULL, NULL, 0,
            "Server starting on port %d with CORS enabled", SERVER_PORT);

    if (!ensure_directory(SERVE_DIR)) {
        fprintf(stderr, "Failed to create serve directory\n");
        return 1;
    }

    if (!ensure_directory(SAVE_DIR)) {
        fprintf(stderr, "Failed to create save directory\n");
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_msg(LOG_ERROR, NULL, 0, NULL, NULL, 0,
                "Failed to create socket: %s", strerror(errno));
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_msg(LOG_WARN, NULL, 0, NULL, NULL, 0,
                "Failed to set SO_REUSEADDR: %s", strerror(errno));
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = htons(SERVER_PORT)
    };

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        log_msg(LOG_ERROR, NULL, 0, NULL, NULL, 0,
                "Failed to bind to port %d: %s", SERVER_PORT, strerror(errno));
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, MAX_CONNECTIONS) < 0) {
        log_msg(LOG_ERROR, NULL, 0, NULL, NULL, 0,
                "Failed to listen: %s", strerror(errno));
        close(server_fd);
        return 1;
    }

    log_msg(LOG_INFO, NULL, 0, NULL, NULL, 0,
            "Server listening on port %d", SERVER_PORT);
    printf("HTTP File Server running on http://localhost:%d\n", SERVER_PORT);
    printf("Serve directory (GET/HEAD): %s\n", SERVE_DIR);
    printf("Save directory (POST): %s\n", SAVE_DIR);
    printf("CORS: Enabled (Access-Control-Allow-Origin: *)\n");
    printf("Press Ctrl+C to stop\n\n");

    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            log_msg(LOG_ERROR, NULL, 0, NULL, NULL, 0,
                    "Accept failed: %s", strerror(errno));
            continue;
        }

        process_request(client_fd, &client_addr);
        close(client_fd);
    }

    log_msg(LOG_INFO, NULL, 0, NULL, NULL, 0, "Server shutting down");
    close(server_fd);
    log_close();

    printf("\nServer stopped\n");
    return 0;
}
