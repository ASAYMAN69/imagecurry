#ifndef LOGGING_H
#define LOGGING_H

#include <string>

namespace ImageCurry {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static Logger& get_instance();
    void init(const std::string& filename);
    void close();
    void log(LogLevel level, const std::string& client_ip, int client_port,
             const std::string& method, const std::string& path, int status,
             const std::string& message);

private:
    Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    ~Logger();

    FILE* log_fp = nullptr;
    LogLevel min_log_level = LogLevel::INFO;
};

void log_init(const std::string& filename);
void log_close();
void log_msg(LogLevel level, const std::string& client_ip, int client_port,
             const std::string& method, const std::string& path, int status,
             const std::string& message);

}
#endif
