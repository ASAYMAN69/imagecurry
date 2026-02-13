#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>
#include "utils.h"

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

int valid_filename(const char *name) {
    if (!name || !*name) return 0;

    if (strlen(name) > MAX_FILENAME_LEN) return 0;

    if (name[0] == '.' || name[0] == '/' || name[0] == '\\') return 0;

    if (strstr(name, "..")) return 0;
    if (strchr(name, '/')) return 0;
    if (strchr(name, '\\')) return 0;

    for (const char *p = name; *p; p++) {
        if (!(isalnum(*p) || *p == '.' || *p == '_' || *p == '-')) {
            return 0;
        }
    }

    return 1;
}

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

void format_http_date(char *out, size_t size, time_t t) {
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, size, "%a, %d %b %Y %H:%M:%S GMT", &tm);
}

void generate_etag(char *out, size_t size, const struct stat *st) {
    snprintf(out, size, "\"%lx-%lx\"",
             (unsigned long)st->st_mtime,
             (unsigned long)st->st_size);
}

const char *get_content_type(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return "application/octet-stream";

    ext++;

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

void build_serve_path(char *out, size_t size, const char *filename) {
    snprintf(out, size, "%s/%s", SERVE_DIR, filename);
}

void build_save_path(char *out, size_t size, const char *filename) {
    snprintf(out, size, "%s/%s", SAVE_DIR, filename);
}
