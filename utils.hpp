#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <ctime>
#include <sys/stat.h>

namespace ImageCurry {

constexpr size_t MAX_FILENAME_LEN = 255;
constexpr const char* SERVE_DIR = "./serve";
constexpr const char* SAVE_DIR = "./save";

std::string url_decode(const std::string& src);
bool valid_filename(const std::string& name);
bool get_query_param(const std::string& query, const std::string& key,
                     std::string& value);
std::string format_http_date(time_t t);
std::string generate_etag(const struct stat& st);
std::string get_content_type(const std::string& filename);
std::string build_serve_path(const std::string& filename);
std::string build_save_path(const std::string& filename);
std::string generate_sha256_uuid();
std::string detect_extension_from_magic(const std::string& body);
std::string detect_extension_from_content_type(const std::string& content_type);

}
#endif
