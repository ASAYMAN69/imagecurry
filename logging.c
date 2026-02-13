#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#include "logging.h"

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
