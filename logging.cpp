#include "logging.hpp"
#include <cstdio>
#include <ctime>
#include <memory>

namespace ImageCurry {

Logger::~Logger() {
    close();
}

Logger& Logger::get_instance() {
    static Logger instance;
    return instance;
}

void Logger::init(const std::string& filename) {
    if (log_fp && log_fp != stderr && log_fp != stdout) {
        fclose(log_fp);
    }

    log_fp = fopen(filename.c_str(), "a");
    if (!log_fp) {
        log_fp = stderr;
    }
}

void Logger::close() {
    if (log_fp && log_fp != stderr && log_fp != stdout) {
        fclose(log_fp);
        log_fp = nullptr;
    }
}

void Logger::log(LogLevel level, const std::string& client_ip, int client_port,
                 const std::string& method, const std::string& path, int status,
                 const std::string& message) {
    if (level < min_log_level || !log_fp) {
        return;
    }

    static const char* level_str[] = {"DEBUG", "INFO", "WARN", "ERROR"};

    time_t now = time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm);

    fprintf(log_fp, "[%s] %-5s | ", timestamp, level_str[static_cast<int>(level)]);

    if (!client_ip.empty()) {
        fprintf(log_fp, "%s:%d | ", client_ip.c_str(), client_port);
    } else {
        fprintf(log_fp, "SYSTEM | ");
    }

    if (!method.empty() && !path.empty()) {
        fprintf(log_fp, "%s %s | %d | ", method.c_str(), path.c_str(), status);
    }

    fprintf(log_fp, "%s\n", message.c_str());
    fflush(log_fp);
}

void log_init(const std::string& filename) {
    Logger::get_instance().init(filename);
}

void log_close() {
    Logger::get_instance().close();
}

void log_msg(LogLevel level, const std::string& client_ip, int client_port,
             const std::string& method, const std::string& path, int status,
             const std::string& message) {
    Logger::get_instance().log(level, client_ip, client_port, method, path, status,
                               message);
}

}
