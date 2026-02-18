#include "handlers.hpp"
#include "http_response.hpp"
#include "utils.hpp"
#include "logging.hpp"
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fstream>
#include <memory>
#include <vector>

namespace ImageCurry {

constexpr const char* CORS_HEADERS =
    "Access-Control-Allow-Origin: *\r\n"
    "Access-Control-Allow-Methods: GET, POST, HEAD, OPTIONS\r\n"
    "Access-Control-Allow-Headers: Content-Type, Content-Length, If-None-Match, If-Modified-Since, Authorization\r\n"
    "Access-Control-Expose-Headers: Content-Length, Content-Type\r\n"
    "Access-Control-Max-Age: 86400\r\n"
    "Vary: Origin\r\n";

void handle_options(int fd, const std::string& client_ip, int client_port) {
    std::string header =
        "HTTP/1.1 204 No Content\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, HEAD, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Content-Length, If-None-Match, If-Modified-Since, Authorization\r\n"
        "Access-Control-Expose-Headers: Content-Length, Content-Type\r\n"
        "Access-Control-Max-Age: 86400\r\n"
        "Vary: Origin\r\n"
        "Connection: close\r\n"
        "\r\n";

    send(fd, header.data(), header.size(), 0);

    log_msg(LogLevel::INFO, client_ip, client_port, "OPTIONS", "*", 204,
            "CORS preflight");
}

void handle_get(int fd, const std::string& request, const std::string& filename,
                const std::string& client_ip, int client_port) {
    std::string filepath = build_serve_path(filename);

    struct stat st;
    if (stat(filepath.c_str(), &st) != 0) {
        log_msg(LogLevel::INFO, client_ip, client_port, "GET", filename, 404,
                "File not found in serve directory: " + filename);
        send_error(fd, 404, "File not found");
        return;
    }

    std::string last_modified = format_http_date(st.st_mtime);
    std::string etag = generate_etag(st);

    auto if_none_match_pos = request.find("If-None-Match:");
    if (if_none_match_pos != std::string::npos) {
        auto etag_pos = request.find(etag, if_none_match_pos);
        if (etag_pos != std::string::npos) {
            log_msg(LogLevel::INFO, client_ip, client_port, "GET", filename, 304,
                    "Cache hit (ETag)");
            send_not_modified(fd, etag, last_modified);
            return;
        }
    }

    auto if_modified_pos = request.find("If-Modified-Since:");
    if (if_modified_pos != std::string::npos) {
        auto mod_pos = request.find(last_modified, if_modified_pos);
        if (mod_pos != std::string::npos) {
            log_msg(LogLevel::INFO, client_ip, client_port, "GET", filename, 304,
                    "Cache hit (Last-Modified)");
            send_not_modified(fd, etag, last_modified);
            return;
        }
    }

    std::ifstream f(filepath, std::ios::binary);
    if (!f) {
        log_msg(LogLevel::ERROR, client_ip, client_port, "GET", filename, 500,
                "Failed to open file: " + std::string(strerror(errno)));
        send_error(fd, 500, "Internal server error");
        return;
    }

    std::string content_type = get_content_type(filename);
    std::string extra =
        "Last-Modified: " + last_modified + "\r\n" +
        "ETag: " + etag + "\r\n" +
        "Cache-Control: public, max-age=31536000, immutable";

    std::string header = "HTTP/1.1 200 OK\r\n";
    header += CORS_HEADERS;
    header += "Content-Type: " + content_type + "\r\n";
    header += "Content-Length: " + std::to_string(st.st_size) + "\r\n";
    header += extra + "\r\n";
    header += "Connection: close\r\n\r\n";

    if (send(fd, header.data(), header.size(), 0) < 0) {
        log_msg(LogLevel::ERROR, client_ip, client_port, "GET", filename, 500,
                "Failed to send headers: " + std::string(strerror(errno)));
        return;
    }

    std::vector<char> buffer(BUFFER_SIZE);
    size_t total_sent = 0;

    while (f.good()) {
        f.read(buffer.data(), buffer.size());
        std::streamsize n = f.gcount();

        if (n > 0) {
            ssize_t sent = send(fd, buffer.data(), n, 0);
            if (sent < 0) {
                log_msg(LogLevel::ERROR, client_ip, client_port, "GET", filename, 500,
                        "Failed to send data at offset " + std::to_string(total_sent) +
                        ": " + std::string(strerror(errno)));
                break;
            }
            total_sent += sent;
        }
    }

    log_msg(LogLevel::INFO, client_ip, client_port, "GET", filename, 200,
            "Sent " + std::to_string(total_sent) + " bytes from serve directory");
}

void compress_to_webp_background(const std::string& input_path,
                                  const std::string& output_path) {
    char exe_path[4096];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len == -1) {
        log_msg(LogLevel::ERROR, "", 0, "", "", 0,
                "Failed to get executable path");
        return;
    }
    exe_path[len] = '\0';

    char* last_slash = strrchr(exe_path, '/');
    if (last_slash) {
        *last_slash = '\0';
    }

    std::string compressor_path = std::string(exe_path) + "/compressor.sh";

    struct stat st;
    if (stat(compressor_path.c_str(), &st) != 0) {
        log_msg(LogLevel::ERROR, "", 0, "", "", 0,
                "compressor.sh not found at " + compressor_path);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        signal(SIGCHLD, SIG_DFL);
        chdir(exe_path);
        sleep(1);

        std::string cmd = "'" + std::string(exe_path) + "/compressor.sh' '" +
                          input_path + "' '" + output_path + "'";
        system(cmd.c_str());
        _exit(0);
    } else if (pid < 0) {
        log_msg(LogLevel::ERROR, "", 0, "", "", 0,
                "Fork failed for compression");
    }
}

void handle_post(int fd, const std::string& request, const std::string& filename,
                 const std::string& body, size_t body_len,
                 const std::string& client_ip, int client_port) {
    (void)request;

    if (body_len > MAX_FILE_SIZE) {
        log_msg(LogLevel::WARN, client_ip, client_port, "POST", filename, 413,
                "File too large: " + std::to_string(body_len) +
                " bytes (max: " + std::to_string(MAX_FILE_SIZE) + ")");
        send_error(fd, 413, "File too large");
        return;
    }

    std::string filepath = build_save_path(filename);
    std::string temppath = filepath + ".tmp";

    {
        std::ofstream f(temppath, std::ios::binary);
        if (!f) {
            log_msg(LogLevel::ERROR, client_ip, client_port, "POST", filename, 500,
                    "Failed to create file: " + std::string(strerror(errno)));
            send_error(fd, 500, "Failed to create file");
            return;
        }

        f.write(body.data(), body_len);
        if (!f) {
            log_msg(LogLevel::ERROR, client_ip, client_port, "POST", filename, 500,
                    "Write failed: " + std::to_string(f.tellp()) + "/" +
                    std::to_string(body_len));
            unlink(temppath.c_str());
            send_error(fd, 500, "Write failed");
            return;
        }
    }

    if (rename(temppath.c_str(), filepath.c_str()) != 0) {
        log_msg(LogLevel::ERROR, client_ip, client_port, "POST", filename, 500,
                "Failed to rename file: " + std::string(strerror(errno)));
        unlink(temppath.c_str());
        send_error(fd, 500, "Failed to save file");
        return;
    }

    chmod(filepath.c_str(), 0600);

    auto last_dot = filename.rfind('.');
    if (last_dot == std::string::npos) {
        log_msg(LogLevel::ERROR, client_ip, client_port, "POST", filename, 400,
                "Filename missing extension");
        send_error(fd, 400, "Filename must have extension");
        return;
    }

    std::string name_part = filename.substr(0, last_dot);
    std::string webp_filename = name_part + ".webp";
    std::string webp_path = std::string(SERVE_DIR) + "/" + webp_filename;

    compress_to_webp_background(filepath, webp_path);

    log_msg(LogLevel::INFO, client_ip, client_port, "POST", filename, 200,
            "Uploaded " + std::to_string(body_len) +
            " bytes to save directory, compressing to " + webp_filename);

    std::string response_body = "{\"status\":\"success\"}";
    send_response(fd, 200, "OK", "application/json", "", response_body);
}

void handle_head(int fd, const std::string& filename,
                 const std::string& client_ip, int client_port) {
    std::string filepath = build_serve_path(filename);

    struct stat st;
    if (stat(filepath.c_str(), &st) != 0) {
        log_msg(LogLevel::INFO, client_ip, client_port, "HEAD", filename, 404,
                "File not found in serve directory");
        send_error(fd, 404, "File not found");
        return;
    }

    std::string last_modified = format_http_date(st.st_mtime);
    std::string etag = generate_etag(st);
    std::string content_type = get_content_type(filename);

    std::string extra =
        "Last-Modified: " + last_modified + "\r\n" +
        "ETag: " + etag + "\r\n" +
        "Cache-Control: public, max-age=31536000, immutable";

    std::string header = "HTTP/1.1 200 OK\r\n";
    header += CORS_HEADERS;
    header += "Content-Type: " + content_type + "\r\n";
    header += "Content-Length: " + std::to_string(st.st_size) + "\r\n";
    header += extra + "\r\n";
    header += "Connection: close\r\n\r\n";

    send(fd, header.data(), header.size(), 0);

    log_msg(LogLevel::INFO, client_ip, client_port, "HEAD", filename, 200,
            "Metadata sent from serve directory");
}

}
