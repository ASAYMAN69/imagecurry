/*
 * Production-Ready HTTP File Server with CORS Support
 * 
 * A secure HTTP/1.1 file server supporting GET, POST, HEAD, and OPTIONS operations
 * with comprehensive security, CORS support, error handling, and caching.
 * 
 * Features:
 * - Full CORS support with * origin
 * - Path traversal prevention
 * - Input validation at all layers
 * - HTTP caching (ETag, Last-Modified)
 * - Comprehensive logging
 * - Proper error codes
 * - File size limits
 * - Content-Type detection
 * 
 * Compilation:
 *   gcc -o fileserver server.c -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L
 * 
 * Usage:
 *   ./fileserver
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <fcntl.h>

/* ==================== Configuration ==================== */

#define SERVER_PORT         8080
#define MAX_CONNECTIONS     128
#define BUFFER_SIZE         8192
#define MAX_FILENAME_LEN    255
#define MAX_FILE_SIZE       (100 * 1024 * 1024)  // 100MB
#define STORAGE_DIR         "./storage"
#define LOG_FILE            "./server.log"
#define REQUEST_TIMEOUT     30
#define MAX_REQUEST_SIZE    (10 * 1024 * 1024)   // 10MB max request

/* ==================== CORS Headers ==================== */

#define CORS_HEADERS \
    "Access-Control-Allow-Origin: *\r\n" \
    "Access-Control-Allow-Methods: GET, POST, HEAD, OPTIONS\r\n" \
    "Access-Control-Allow-Headers: Content-Type, Content-Length, If-None-Match, If-Modified-Since\r\n" \
    "Access-Control-Max-Age: 86400\r\n"

/* ==================== Logging ==================== */

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;

static FILE *log_fp = NULL;
static LogLevel min_log_level = LOG_INFO;

void log_init(const char *filename) {
    log_fp = fopen(filename, "a");
    if (!log_fp) {
        fprintf(stderr, "Warning: Could not open log file %s: %s\n", 
                filename, strerror(errno));
        log_fp = stderr;
    }
}

void log_close(void) {
    if (log_fp && log_fp != stderr && log_fp != stdout) {
        fclose(log_fp);
    }
}

void log_msg(LogLevel level, const char *client_ip, int client_port,
             const char *method, const char *path, int status, 
             const char *fmt, ...) {
    if (level < min_log_level) return;
    
    const char *level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};
    
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm);
    
    fprintf(log_fp, "[%s] %-5s | ", timestamp, level_str[level]);
    
    if (client_ip) {
        fprintf(log_fp, "%s:%d | ", client_ip, client_port);
    } else {
        fprintf(log_fp, "SYSTEM | ");
    }
    
    if (method && path) {
        fprintf(log_fp, "%s %s | %d | ", method, path, status);
    }
    
    va_list args;
    va_start(args, fmt);
    vfprintf(log_fp, fmt, args);
    va_end(args);
    
    fprintf(log_fp, "\n");
    fflush(log_fp);
}

/* ==================== Utilities ==================== */

/**
 * URL decode a string in place
 */
void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if (*src == '%' && (a = src[1]) && (b = src[2]) && isxdigit(a) && isxdigit(b)) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16 * a + b;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/**
 * Validate filename - prevent path traversal and only allow safe characters
 */
int valid_filename(const char *name) {
    if (!name || !*name) return 0;
    
    // Check length
    if (strlen(name) > MAX_FILENAME_LEN) return 0;
    
    // No leading dots or slashes
    if (name[0] == '.' || name[0] == '/' || name[0] == '\\') return 0;
    
    // Check for path traversal
    if (strstr(name, "..")) return 0;
    if (strchr(name, '/')) return 0;
    if (strchr(name, '\\')) return 0;
    
    // Only allow alphanumeric, dot, underscore, hyphen
    int dot_count = 0;
    for (const char *p = name; *p; p++) {
        if (*p == '.') {
            dot_count++;
            if (dot_count > 1) return 0;  // Only one dot allowed
        } else if (!(isalnum(*p) || *p == '_' || *p == '-')) {
            return 0;
        }
    }
    
    return 1;
}

/**
 * Extract query parameter from URL
 */
int get_query_param(const char *query, const char *key, char *value, size_t value_size) {
    if (!query || !key || !value) return 0;
    
    char search[MAX_FILENAME_LEN + 10];
    snprintf(search, sizeof(search), "%s=", key);
    
    const char *start = strstr(query, search);
    if (!start) return 0;
    
    start += strlen(search);
    const char *end = strpbrk(start, " &\r\n");
    size_t len = end ? (size_t)(end - start) : strlen(start);
    
    if (len == 0 || len >= value_size) return 0;
    
    char encoded[MAX_FILENAME_LEN];
    memcpy(encoded, start, len);
    encoded[len] = '\0';
    
    url_decode(value, encoded);
    return 1;
}

/**
 * Format time as HTTP date
 */
void format_http_date(char *out, size_t size, time_t t) {
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, size, "%a, %d %b %Y %H:%M:%S GMT", &tm);
}

/**
 * Generate ETag from file stats
 */
void generate_etag(char *out, size_t size, const struct stat *st) {
    snprintf(out, size, "\"%lx-%lx\"", 
             (unsigned long)st->st_mtime, 
             (unsigned long)st->st_size);
}

/**
 * Get MIME type from filename extension
 */
const char *get_content_type(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return "application/octet-stream";
    
    ext++; // Skip the dot
    
    if (strcasecmp(ext, "txt") == 0) return "text/plain";
    if (strcasecmp(ext, "html") == 0) return "text/html";
    if (strcasecmp(ext, "css") == 0) return "text/css";
    if (strcasecmp(ext, "js") == 0) return "application/javascript";
    if (strcasecmp(ext, "json") == 0) return "application/json";
    if (strcasecmp(ext, "xml") == 0) return "application/xml";
    if (strcasecmp(ext, "pdf") == 0) return "application/pdf";
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) 
        return "image/jpeg";
    if (strcasecmp(ext, "png") == 0) return "image/png";
    if (strcasecmp(ext, "gif") == 0) return "image/gif";
    if (strcasecmp(ext, "svg") == 0) return "image/svg+xml";
    if (strcasecmp(ext, "zip") == 0) return "application/zip";
    
    return "application/octet-stream";
}

/**
 * Build full file path in storage directory
 */
void build_file_path(char *out, size_t size, const char *filename) {
    snprintf(out, size, "%s/%s", STORAGE_DIR, filename);
}

/* ==================== HTTP Response Helpers ==================== */

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

/* ==================== Request Handlers ==================== */

/**
 * Handle OPTIONS request - CORS preflight
 */
void handle_options(int fd, const char *client_ip, int client_port) {
    char header[512];
    int len = snprintf(header, sizeof(header),
        "HTTP/1.1 204 No Content\r\n"
        CORS_HEADERS
        "Connection: close\r\n"
        "\r\n");
    
    send(fd, header, len, 0);
    
    log_msg(LOG_INFO, client_ip, client_port, "OPTIONS", "*", 204,
            "CORS preflight");
}

/**
 * Handle GET request - download file
 */
void handle_get(int fd, const char *request, const char *filename,
                const char *client_ip, int client_port) {
    char filepath[512];
    build_file_path(filepath, sizeof(filepath), filename);
    
    struct stat st;
    if (stat(filepath, &st) != 0) {
        log_msg(LOG_INFO, client_ip, client_port, "GET", filename, 404,
                "File not found: %s", filename);
        send_error(fd, 404, "File not found");
        return;
    }
    
    // Generate cache headers
    char last_modified[64], etag[64];
    format_http_date(last_modified, sizeof(last_modified), st.st_mtime);
    generate_etag(etag, sizeof(etag), &st);
    
    // Check If-None-Match (ETag)
    char *if_none_match = strstr(request, "If-None-Match:");
    if (if_none_match) {
        if (strstr(if_none_match, etag)) {
            log_msg(LOG_INFO, client_ip, client_port, "GET", filename, 304,
                    "Cache hit (ETag)");
            send_not_modified(fd, etag, last_modified);
            return;
        }
    }
    
    // Check If-Modified-Since
    char *if_modified = strstr(request, "If-Modified-Since:");
    if (if_modified) {
        if (strstr(if_modified, last_modified)) {
            log_msg(LOG_INFO, client_ip, client_port, "GET", filename, 304,
                    "Cache hit (Last-Modified)");
            send_not_modified(fd, etag, last_modified);
            return;
        }
    }
    
    // Open file
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        log_msg(LOG_ERROR, client_ip, client_port, "GET", filename, 500,
                "Failed to open file: %s", strerror(errno));
        send_error(fd, 500, "Internal server error");
        return;
    }
    
    // Send headers
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
        CORS_HEADERS
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
    
    // Stream file content
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
            "Sent %zu bytes", total_sent);
}

/**
 * Handle POST request - upload file
 */
void handle_post(int fd, const char *request, const char *filename,
                 const char *body, size_t body_len,
                 const char *client_ip, int client_port) {
    
    // Validate file size
    if (body_len > MAX_FILE_SIZE) {
        log_msg(LOG_WARN, client_ip, client_port, "POST", filename, 413,
                "File too large: %zu bytes (max: %d)", body_len, MAX_FILE_SIZE);
        send_error(fd, 413, "File too large");
        return;
    }
    
    char filepath[512];
    build_file_path(filepath, sizeof(filepath), filename);
    
    // Write file atomically (write to temp, then rename)
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
    
    // Atomic rename
    if (rename(temppath, filepath) != 0) {
        log_msg(LOG_ERROR, client_ip, client_port, "POST", filename, 500,
                "Failed to rename file: %s", strerror(errno));
        unlink(temppath);
        send_error(fd, 500, "Failed to save file");
        return;
    }
    
    // Set file permissions to be restrictive
    chmod(filepath, 0600);
    
    log_msg(LOG_INFO, client_ip, client_port, "POST", filename, 200,
            "Uploaded %zu bytes", body_len);
    
    char response_body[] = "{\"status\":\"success\"}";
    send_response(fd, 200, "OK", "application/json", NULL, 
                  response_body, strlen(response_body));
}

/**
 * Handle HEAD request - get metadata only
 */
void handle_head(int fd, const char *filename,
                 const char *client_ip, int client_port) {
    char filepath[512];
    build_file_path(filepath, sizeof(filepath), filename);
    
    struct stat st;
    if (stat(filepath, &st) != 0) {
        log_msg(LOG_INFO, client_ip, client_port, "HEAD", filename, 404,
                "File not found");
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
        CORS_HEADERS
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "%s"
        "Connection: close\r\n"
        "\r\n",
        content_type, st.st_size, extra);
    
    send(fd, header, hlen, 0);
    
    log_msg(LOG_INFO, client_ip, client_port, "HEAD", filename, 200,
            "Metadata sent");
}

/* ==================== Request Processing ==================== */

void process_request(int client_fd, struct sockaddr_in *client_addr) {
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr->sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr->sin_port);
    
    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = REQUEST_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // Read headers first
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

    // Parse request line
    char method[16] = {0};
    char path[512] = {0};
    char version[16] = {0};
    
    if (sscanf(header_buf, "%15s %511s %15s", method, path, version) != 3) {
        log_msg(LOG_WARN, client_ip, client_port, "INVALID", path, 400,
                "Malformed request");
        send_error(client_fd, 400, "Malformed request");
        return;
    }
    
    // Validate HTTP version
    if (strcmp(version, "HTTP/1.1") != 0 && strcmp(version, "HTTP/1.0") != 0) {
        log_msg(LOG_WARN, client_ip, client_port, method, path, 400,
                "Invalid HTTP version: %s", version);
        send_error(client_fd, 400, "Invalid HTTP version");
        return;
    }
    
    // Handle OPTIONS request (CORS preflight)
    if (strcmp(method, "OPTIONS") == 0) {
        handle_options(client_fd, client_ip, client_port);
        return;
    }
    
    // Only accept requests to root path
    char *query = strchr(path, '?');
    size_t path_len = query ? (size_t)(query - path) : strlen(path);
    
    if (path_len != 1 || path[0] != '/') {
        log_msg(LOG_WARN, client_ip, client_port, method, path, 400,
                "Invalid path");
        send_error(client_fd, 400, "Invalid path - only / is supported");
        return;
    }
    
    // Extract filename from query
    char filename[MAX_FILENAME_LEN + 1] = {0};
    if (!query || !get_query_param(query + 1, "name", filename, sizeof(filename))) {
        log_msg(LOG_WARN, client_ip, client_port, method, path, 400,
                "Missing 'name' parameter");
        send_error(client_fd, 400, "Missing 'name' parameter");
        return;
    }
    
    // Validate filename
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
    
    // Route to handler
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

/* ==================== Server Lifecycle ==================== */

static volatile int server_running = 1;

void signal_handler(int signum) {
    (void)signum;
    server_running = 0;
}

int main(void) {
    // Initialize logging
    log_init(LOG_FILE);
    log_msg(LOG_INFO, NULL, 0, NULL, NULL, 0, 
            "Server starting on port %d with CORS enabled", SERVER_PORT);
    
    // Create storage directory
    struct stat st;
    if (stat(STORAGE_DIR, &st) != 0) {
        if (mkdir(STORAGE_DIR, 0755) != 0) {
            log_msg(LOG_ERROR, NULL, 0, NULL, NULL, 0,
                    "Failed to create storage directory: %s", strerror(errno));
            return 1;
        }
        log_msg(LOG_INFO, NULL, 0, NULL, NULL, 0,
                "Created storage directory: %s", STORAGE_DIR);
    }
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);  // Ignore broken pipe
    
    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        log_msg(LOG_ERROR, NULL, 0, NULL, NULL, 0,
                "Failed to create socket: %s", strerror(errno));
        return 1;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_msg(LOG_WARN, NULL, 0, NULL, NULL, 0,
                "Failed to set SO_REUSEADDR: %s", strerror(errno));
    }
    
    // Bind socket
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
    
    // Listen
    if (listen(server_fd, MAX_CONNECTIONS) < 0) {
        log_msg(LOG_ERROR, NULL, 0, NULL, NULL, 0,
                "Failed to listen: %s", strerror(errno));
        close(server_fd);
        return 1;
    }
    
    log_msg(LOG_INFO, NULL, 0, NULL, NULL, 0,
            "Server listening on port %d", SERVER_PORT);
    printf("HTTP File Server running on http://localhost:%d\n", SERVER_PORT);
    printf("Storage directory: %s\n", STORAGE_DIR);
    printf("CORS: Enabled (Access-Control-Allow-Origin: *)\n");
    printf("Press Ctrl+C to stop\n\n");
    
    // Main server loop
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;  // Interrupted by signal
            log_msg(LOG_ERROR, NULL, 0, NULL, NULL, 0,
                    "Accept failed: %s", strerror(errno));
            continue;
        }
        
        process_request(client_fd, &client_addr);
        close(client_fd);
    }
    
    // Cleanup
    log_msg(LOG_INFO, NULL, 0, NULL, NULL, 0, "Server shutting down");
    close(server_fd);
    log_close();
    
    printf("\nServer stopped\n");
    return 0;
}