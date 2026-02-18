#include "logging.hpp"
#include "http_response.hpp"
#include "handlers.hpp"
#include "utils.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netdb.h>

namespace ImageCurry {

constexpr int SERVER_PORT = 8080;
constexpr int MAX_CONNECTIONS = 128;
constexpr int REQUEST_TIMEOUT = 30;
constexpr size_t MAX_REQUEST_SIZE = 128 * 1024 * 1024;
constexpr const char* LOG_FILE = "./server.log";

class ScopedFileDescriptor {
public:
    explicit ScopedFileDescriptor(int fd = -1) : fd_(fd) {}
    ~ScopedFileDescriptor() { close_fd(); }

    ScopedFileDescriptor(const ScopedFileDescriptor&) = delete;
    ScopedFileDescriptor& operator=(const ScopedFileDescriptor&) = delete;

    ScopedFileDescriptor(ScopedFileDescriptor&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }

    ScopedFileDescriptor& operator=(ScopedFileDescriptor&& other) noexcept {
        if (this != &other) {
            close_fd();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    int get() const { return fd_; }
    int release() { int tmp = fd_; fd_ = -1; return tmp; }
    explicit operator bool() const { return fd_ >= 0; }

private:
    void close_fd() {
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
    }

    int fd_;
};

static volatile sig_atomic_t server_running = 1;

void signal_handler(int signum) {
    (void)signum;
    server_running = 0;
}

bool ensure_directory(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        if (mkdir(path.c_str(), 0755) != 0) {
            log_msg(LogLevel::ERROR, "", 0, "", "", 0,
                    "Failed to create directory " + path + ": " +
                    std::string(strerror(errno)));
            return false;
        }
        log_msg(LogLevel::INFO, "", 0, "", "", 0,
                "Created directory: " + path);
    } else if (!S_ISDIR(st.st_mode)) {
        log_msg(LogLevel::ERROR, "", 0, "", "", 0,
                path + " exists but is not a directory");
        return false;
    }
    return true;
}

void process_request(int client_fd, const std::string& client_ip, int client_port) {
    ScopedFileDescriptor fd_holder(client_fd);

    struct timeval tv;
    tv.tv_sec = REQUEST_TIMEOUT;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::vector<char> buffer(BUFFER_SIZE);
    ssize_t received = 0;
    char* header_end = nullptr;

    while (received < static_cast<ssize_t>(BUFFER_SIZE) - 1) {
        ssize_t n = recv(client_fd, buffer.data() + received, BUFFER_SIZE - received - 1, 0);
        if (n <= 0) {
            if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                log_msg(LogLevel::ERROR, client_ip, client_port, "", "", 0,
                        "Receive error: " + std::string(strerror(errno)));
            }
            return;
        }
        received += n;
        buffer[received] = '\0';
        header_end = strstr(buffer.data(), "\r\n\r\n");
        if (header_end) break;
    }

    if (!header_end) {
        log_msg(LogLevel::WARN, client_ip, client_port, "INVALID", "", 400,
                "Headers too large or malformed");
        send_error(client_fd, 400, "Headers too large or malformed");
        return;
    }

    std::string method, path, version;
    char method_buf[16] = {0}, path_buf[512] = {0}, version_buf[16] = {0};

    if (sscanf(buffer.data(), "%15s %511s %15s", method_buf, path_buf, version_buf) != 3) {
        log_msg(LogLevel::WARN, client_ip, client_port, "INVALID", "", 400,
                "Malformed request");
        send_error(client_fd, 400, "Malformed request");
        return;
    }

    method = method_buf;
    path = path_buf;
    version = version_buf;

    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        log_msg(LogLevel::WARN, client_ip, client_port, method, path, 400,
                "Invalid HTTP version: " + version);
        send_error(client_fd, 400, "Invalid HTTP version");
        return;
    }

    if (method == "OPTIONS") {
        handle_options(client_fd, client_ip, client_port);
        return;
    }

    size_t query_pos = path.find('?');
    std::string path_only = (query_pos != std::string::npos) ? path.substr(0, query_pos) : path;
    std::string query_part = (query_pos != std::string::npos) ? path.substr(query_pos + 1) : "";

    std::string body;
    size_t body_len = 0;
    std::string request_str(buffer.data());

    auto content_len_pos = request_str.find("Content-Length:");
    if (content_len_pos != std::string::npos) {
        size_t content_length = std::stoul(request_str.substr(content_len_pos + 15));

        if (content_length > MAX_REQUEST_SIZE) {
            send_error(client_fd, 413, "Payload Too Large");
            return;
        }

        if (content_length > 0) {
            body.resize(content_length);

            size_t header_len = (header_end - buffer.data()) + 4;
            ssize_t body_part_len = received - header_len;

            if (body_part_len > 0) {
                std::memcpy(body.data(), header_end + 4, body_part_len);
            }
            body_len = (body_part_len > 0) ? body_part_len : 0;

            while (body_len < content_length) {
                ssize_t n = recv(client_fd, body.data() + body_len,
                                 content_length - body_len, 0);
                if (n <= 0) {
                    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        log_msg(LogLevel::ERROR, client_ip, client_port, "", "", 0,
                                "Receive error: " + std::string(strerror(errno)));
                    }
                    return;
                }
                body_len += n;
            }
        }
    }

    if (method == "POST") {
        if (path_only != "/upload") {
            log_msg(LogLevel::WARN, client_ip, client_port, method, path, 400,
                    "Invalid path for POST - only /upload is supported");
            send_error(client_fd, 400, "Invalid path - POST only accepts /upload");
            return;
        }
        handle_upload(client_fd, request_str, body, body_len, client_ip, client_port);
        return;
    } else if (method == "GET" || method == "HEAD") {
        if (path_only != "/retrieve") {
            log_msg(LogLevel::WARN, client_ip, client_port, method, path, 400,
                    "Invalid path - GET/HEAD only accepts /retrieve");
            send_error(client_fd, 400, "Invalid path - GET/HEAD only accepts /retrieve");
            return;
        }

        std::string filename;
        if (query_part.empty() || !get_query_param(query_part, "name", filename)) {
            log_msg(LogLevel::WARN, client_ip, client_port, method, path, 400,
                    "Missing 'name' parameter");
            send_error(client_fd, 400, "Missing 'name' parameter");
            return;
        }

        if (!valid_filename(filename)) {
            log_msg(LogLevel::WARN, client_ip, client_port, method, path, 400,
                    "Invalid filename: " + filename);
            send_error(client_fd, 400, "Invalid filename");
            return;
        }

        bool is_head = (method == "HEAD");
        handle_retrieve(client_fd, request_str, filename, client_ip, client_port, is_head);
        return;
    } else {
        log_msg(LogLevel::WARN, client_ip, client_port, method, path, 501,
                "Method not implemented");
        send_error(client_fd, 501, "Method not implemented");
        return;
    }
}

}

int main(void) {
    using namespace ImageCurry;

    log_init(LOG_FILE);
    log_msg(LogLevel::INFO, "", 0, "", "", 0,
            "Server starting on port " + std::to_string(SERVER_PORT) + " with CORS enabled");

    if (!ensure_directory(SERVE_DIR)) {
        std::cerr << "Failed to create serve directory\n";
        return 1;
    }

    if (!ensure_directory(SAVE_DIR)) {
        std::cerr << "Failed to create save directory\n";
        return 1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);

    ScopedFileDescriptor server_fd(socket(AF_INET, SOCK_STREAM, 0));
    if (!server_fd) {
        log_msg(LogLevel::ERROR, "", 0, "", "", 0,
                "Failed to create socket: " + std::string(strerror(errno)));
        return 1;
    }

    int opt = 1;
    if (setsockopt(server_fd.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        log_msg(LogLevel::WARN, "", 0, "", "", 0,
                "Failed to set SO_REUSEADDR: " + std::string(strerror(errno)));
    }

    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_fd.get(), (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        log_msg(LogLevel::ERROR, "", 0, "", "", 0,
                "Failed to bind to port " + std::to_string(SERVER_PORT) + ": " +
                std::string(strerror(errno)));
        return 1;
    }

    if (listen(server_fd.get(), MAX_CONNECTIONS) < 0) {
        log_msg(LogLevel::ERROR, "", 0, "", "", 0,
                "Failed to listen: " + std::string(strerror(errno)));
        return 1;
    }

    log_msg(LogLevel::INFO, "", 0, "", "", 0,
            "Server listening on port " + std::to_string(SERVER_PORT));
    std::cout << "HTTP File Server running on http://localhost:" << SERVER_PORT << "\n";
    std::cout << "Upload endpoint: POST /upload\n";
    std::cout << "Retrieve endpoint: GET/HEAD /retrieve?name=<filename>\n";
    std::cout << "Serve directory (GET/HEAD): " << SERVE_DIR << "\n";
    std::cout << "Save directory (POST): " << SAVE_DIR << "\n";
    std::cout << "CORS: Enabled (Access-Control-Allow-Origin: *)\n";
    std::cout << "Press Ctrl+C to stop\n\n";

    while (server_running) {
        struct sockaddr_in client_addr = {};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd.get(), (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            log_msg(LogLevel::ERROR, "", 0, "", "", 0,
                    "Accept failed: " + std::string(strerror(errno)));
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        int client_port = ntohs(client_addr.sin_port);

        process_request(client_fd, std::string(client_ip), client_port);
    }

    log_msg(LogLevel::INFO, "", 0, "", "", 0, "Server shutting down");
    log_close();

    std::cout << "\nServer stopped\n";
    return 0;
}
