#include "utils.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <unordered_map>

namespace ImageCurry {

std::string url_decode(const std::string& src) {
    std::string result;
    result.reserve(src.size());

    for (size_t i = 0; i < src.size(); ) {
        if (src[i] == '%' && i + 2 < src.size() &&
            isxdigit(src[i + 1]) && isxdigit(src[i + 2])) {
            char a = src[i + 1];
            char b = src[i + 2];

            auto hex_to_int = [](char c) -> int {
                c = toupper(c);
                if (c >= 'A') return c - 'A' + 10;
                return c - '0';
            };

            result += static_cast<char>(hex_to_int(a) * 16 + hex_to_int(b));
            i += 3;
        } else if (src[i] == '+') {
            result += ' ';
            i++;
        } else {
            result += src[i];
            i++;
        }
    }
    return result;
}

bool valid_filename(const std::string& name) {
    if (name.empty() || name.size() > MAX_FILENAME_LEN) {
        return false;
    }

    if (name[0] == '.' || name[0] == '/' || name[0] == '\\') {
        return false;
    }

    if (name.find("..") != std::string::npos ||
        name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos) {
        return false;
    }

    return std::all_of(name.begin(), name.end(), [](char c) {
        return isalnum(c) || c == '.' || c == '_' || c == '-';
    });
}

bool get_query_param(const std::string& query, const std::string& key,
                     std::string& value) {
    std::string search = key + "=";
    auto pos = query.find(search);

    if (pos == std::string::npos) {
        return false;
    }

    size_t start = pos + search.size();
    size_t end = query.find_first_of(" &\r\n", start);

    std::string encoded;
    if (end != std::string::npos) {
        encoded = query.substr(start, end - start);
    } else {
        encoded = query.substr(start);
    }

    if (encoded.empty()) {
        return false;
    }

    value = url_decode(encoded);
    return true;
}

std::string format_http_date(time_t t) {
    struct tm tm;
    gmtime_r(&t, &tm);

    char buffer[64];
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &tm);
    return std::string(buffer);
}

std::string generate_etag(const struct stat& st) {
    std::ostringstream oss;
    oss << "\"" << st.st_mtime << "-" << st.st_size << "\"";
    return oss.str();
}

std::string get_content_type(const std::string& filename) {
    auto pos = filename.rfind('.');
    if (pos == std::string::npos) {
        return "application/octet-stream";
    }

    std::string ext = filename.substr(pos + 1);
    std::string ext_lower = ext;
    std::transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(),
                   [](char c) { return tolower(c); });

    static const std::unordered_map<std::string, std::string> CONTENT_TYPES = {
        {"txt", "text/plain"},
        {"html", "text/html"},
        {"css", "text/css"},
        {"js", "application/javascript"},
        {"json", "application/json"},
        {"xml", "application/xml"},
        {"pdf", "application/pdf"},
        {"jpg", "image/jpeg"},
        {"jpeg", "image/jpeg"},
        {"png", "image/png"},
        {"gif", "image/gif"},
        {"svg", "image/svg+xml"},
        {"webp", "image/webp"},
        {"zip", "application/zip"}
    };

    auto it = CONTENT_TYPES.find(ext_lower);
    if (it != CONTENT_TYPES.end()) {
        return it->second;
    }

    return "application/octet-stream";
}

std::string build_serve_path(const std::string& filename) {
    return std::string(SERVE_DIR) + "/" + filename;
}

std::string build_save_path(const std::string& filename) {
    return std::string(SAVE_DIR) + "/" + filename;
}

}
