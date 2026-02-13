#ifndef LOGGING_H
#define LOGGING_H

#include <time.h>

typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;

void log_init(const char *filename);
void log_close(void);
void log_msg(LogLevel level, const char *client_ip, int client_port,
             const char *method, const char *path, int status,
             const char *fmt, ...);

#endif
