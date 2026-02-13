#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <signal.h>
#include "handlers.h"
#include "http_response.h"
#include "utils.h"
#include "logging.h"

void handle_options(int fd, const char *client_ip, int client_port) {
    char header[512];
    int len = snprintf(header, sizeof(header),
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, HEAD, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Content-Length, If-None-Match, If-Modified-Since, Authorization\r\n"
        "Access-Control-Expose-Headers: Content-Length, Content-Type\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "Vary: Origin\r\n"
        "Connection: close\r\n"
        "\r\n");

    send(fd, header, len, 0);

    log_msg(LOG_INFO, client_ip, client_port, "OPTIONS", "*", 204,
            "CORS preflight");
}

void handle_get(int fd, const char *request, const char *filename,
                const char *client_ip, int client_port) {
    char filepath[512];
    build_serve_path(filepath, sizeof(filepath), filename);

    struct stat st;
    if (stat(filepath, &st) != 0) {
        log_msg(LOG_INFO, client_ip, client_port, "GET", filename, 404,
                "File not found in serve directory: %s", filename);
        send_error(fd, 404, "File not found");
        return;
    }

    char last_modified[64], etag[64];
    format_http_date(last_modified, sizeof(last_modified), st.st_mtime);
    generate_etag(etag, sizeof(etag), &st);

    char *if_none_match = strstr(request, "If-None-Match:");
    if (if_none_match) {
        if (strstr(if_none_match, etag)) {
            log_msg(LOG_INFO, client_ip, client_port, "GET", filename, 304,
                    "Cache hit (ETag)");
            send_not_modified(fd, etag, last_modified);
            return;
        }
    }

    char *if_modified = strstr(request, "If-Modified-Since:");
    if (if_modified) {
        if (strstr(if_modified, last_modified)) {
            log_msg(LOG_INFO, client_ip, client_port, "GET", filename, 304,
                    "Cache hit (Last-Modified)");
            send_not_modified(fd, etag, last_modified);
            return;
        }
    }

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        log_msg(LOG_ERROR, client_ip, client_port, "GET", filename, 500,
                "Failed to open file: %s", strerror(errno));
        send_error(fd, 500, "Internal server error");
        return;
    }

    const char *content_type = get_content_type(filename);
    char extra[512];
    snprintf(extra, sizeof(extra),
        "Last-Modified: %s\r\n"
        "ETag: %s\r\n"
        "Cache-Control: public, max-age=31536000, immutable\r\n",
        last_modified, etag);

    char header[1024];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, HEAD, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Content-Length, If-None-Match, If-Modified-Since, Authorization\r\n"
        "Access-Control-Expose-Headers: Content-Length, Content-Type\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "Vary: Origin\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "%s"
        "Connection: close\r\n"
        "\r\n",
        content_type, st.st_size, extra);

    if (send(fd, header, hlen, 0) < 0) {
        log_msg(LOG_ERROR, client_ip, client_port, "GET", filename, 500,
                "Failed to send headers: %s", strerror(errno));
        fclose(f);
        return;
    }

    char buffer[BUFFER_SIZE];
    size_t total_sent = 0;
    size_t n;

    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        ssize_t sent = send(fd, buffer, n, 0);
        if (sent < 0) {
            log_msg(LOG_ERROR, client_ip, client_port, "GET", filename, 500,
                    "Failed to send data at offset %zu: %s",
                    total_sent, strerror(errno));
            fclose(f);
            return;
        }
        total_sent += sent;
    }

    fclose(f);

    log_msg(LOG_INFO, client_ip, client_port, "GET", filename, 200,
            "Sent %zu bytes from serve directory", total_sent);
}

void compress_to_webp_background(const char *input_path, const char *output_path) {
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        log_msg(LOG_ERROR, NULL, 0, NULL, NULL, 0, "Failed to get executable path");
        return;
    }
    exe_path[len] = '\0';

    char *last_slash = strrchr(exe_path, '/');
    if (last_slash) {
        *last_slash = '\0';
    }

    char compressor_path[4096];
    int path_len = snprintf(compressor_path, sizeof(compressor_path), "%s/compressor.sh", exe_path);
    if (path_len < 0 || (size_t)path_len >= sizeof(compressor_path)) {
        log_msg(LOG_ERROR, NULL, 0, NULL, NULL, 0, "Path too long for compressor.sh");
        return;
    }

    struct stat st;
    if (stat(compressor_path, &st) != 0) {
        log_msg(LOG_ERROR, NULL, 0, NULL, NULL, 0, "compressor.sh not found at %s", compressor_path);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGCHLD, SIG_DFL);
        chdir(exe_path);
        sleep(1);

        char cmd[8192];
        int cmd_len = snprintf(cmd, sizeof(cmd), "'%s/compressor.sh' '%s' '%s'",
                exe_path, input_path, output_path);
        if (cmd_len < 0 || (size_t)cmd_len >= sizeof(cmd)) {
            _exit(1);
        }
        system(cmd);
        _exit(0);
    } else if (pid < 0) {
        log_msg(LOG_ERROR, NULL, 0, NULL, NULL, 0, "Fork failed for compression");
    }
}

void handle_post(int fd, const char *request, const char *filename,
                 const char *body, size_t body_len,
                 const char *client_ip, int client_port) {
    (void)request;

    if (body_len > MAX_FILE_SIZE) {
        log_msg(LOG_WARN, client_ip, client_port, "POST", filename, 413,
                "File too large: %zu bytes (max: %d)", body_len, MAX_FILE_SIZE);
        send_error(fd, 413, "File too large");
        return;
    }

    char filepath[512];
    build_save_path(filepath, sizeof(filepath), filename);

    char temppath[520];
    snprintf(temppath, sizeof(temppath), "%s.tmp", filepath);

    FILE *f = fopen(temppath, "wb");
    if (!f) {
        log_msg(LOG_ERROR, client_ip, client_port, "POST", filename, 500,
                "Failed to create file: %s", strerror(errno));
        send_error(fd, 500, "Failed to create file");
        return;
    }

    size_t written = fwrite(body, 1, body_len, f);
    if (written != body_len) {
        log_msg(LOG_ERROR, client_ip, client_port, "POST", filename, 500,
                "Write failed: %zu/%zu bytes", written, body_len);
        fclose(f);
        unlink(temppath);
        send_error(fd, 500, "Write failed");
        return;
    }

    if (fclose(f) != 0) {
        log_msg(LOG_ERROR, client_ip, client_port, "POST", filename, 500,
                "Failed to close file: %s", strerror(errno));
        unlink(temppath);
        send_error(fd, 500, "Failed to close file");
        return;
    }

    if (rename(temppath, filepath) != 0) {
        log_msg(LOG_ERROR, client_ip, client_port, "POST", filename, 500,
                "Failed to rename file: %s", strerror(errno));
        unlink(temppath);
        send_error(fd, 500, "Failed to save file");
        return;
    }

    chmod(filepath, 0600);

    char *last_dot = strrchr(filename, '.');
    if (!last_dot) {
        log_msg(LOG_ERROR, client_ip, client_port, "POST", filename, 400,
                "Filename missing extension");
        send_error(fd, 400, "Filename must have extension");
        return;
    }

    size_t name_len = last_dot - filename;
    char webp_filename[MAX_FILENAME_LEN + 5];
    snprintf(webp_filename, sizeof(webp_filename), "%.*s.webp", (int)name_len, filename);

    char webp_path[512];
    snprintf(webp_path, sizeof(webp_path), "%s/%s", SERVE_DIR, webp_filename);

    compress_to_webp_background(filepath, webp_path);

    log_msg(LOG_INFO, client_ip, client_port, "POST", filename, 200,
            "Uploaded %zu bytes to save directory, compressing to %s",
            body_len, webp_filename);

    char response_body[] = "{\"status\":\"success\"}";
    send_response(fd, 200, "OK", "application/json", NULL,
                  response_body, strlen(response_body));
}

void handle_head(int fd, const char *filename,
                 const char *client_ip, int client_port) {
    char filepath[512];
    build_serve_path(filepath, sizeof(filepath), filename);

    struct stat st;
    if (stat(filepath, &st) != 0) {
        log_msg(LOG_INFO, client_ip, client_port, "HEAD", filename, 404,
                "File not found in serve directory");
        send_error(fd, 404, "File not found");
        return;
    }

    char last_modified[64], etag[64];
    format_http_date(last_modified, sizeof(last_modified), st.st_mtime);
    generate_etag(etag, sizeof(etag), &st);

    const char *content_type = get_content_type(filename);
    char extra[512];
    snprintf(extra, sizeof(extra),
        "Last-Modified: %s\r\n"
        "ETag: %s\r\n"
        "Cache-Control: public, max-age=31536000, immutable\r\n",
        last_modified, etag);

    char header[1024];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, HEAD, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Content-Length, If-None-Match, If-Modified-Since, Authorization\r\n"
        "Access-Control-Expose-Headers: Content-Length, Content-Type\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "Vary: Origin\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "%s"
        "Connection: close\r\n"
        "\r\n",
        content_type, st.st_size, extra);

    send(fd, header, hlen, 0);

    log_msg(LOG_INFO, client_ip, client_port, "HEAD", filename, 200,
            "Metadata sent from serve directory");
}
