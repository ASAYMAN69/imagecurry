#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <time.h>
#include <sys/stat.h>

#define MAX_FILENAME_LEN    255
#define SERVE_DIR           "./serve"
#define SAVE_DIR            "./save"

void url_decode(char *dst, const char *src);
int valid_filename(const char *name);
int get_query_param(const char *query, const char *key, char *value, size_t value_size);
void format_http_date(char *out, size_t size, time_t t);
void generate_etag(char *out, size_t size, const struct stat *st);
const char *get_content_type(const char *filename);
void build_serve_path(char *out, size_t size, const char *filename);
void build_save_path(char *out, size_t size, const char *filename);

#endif
