#include "utils.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <ctime>
#include <random>
#include <cstring>
#include <chrono>

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

std::string generate_sha256_uuid() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();

    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;

    std::ostringstream oss;
    oss << std::hex << std::setfill('0') << std::setw(16) << timestamp;
    oss << std::hex << std::setfill('0') << std::setw(16) << dis(gen);
    oss << std::hex << std::setfill('0') << std::setw(16) << dis(gen);
    oss << std::hex << std::setfill('0') << std::setw(16) << dis(gen);

    std::string result = oss.str();

    std::string hash_input = result + std::to_string(time(nullptr));
    uint64_t hash = 0xcbf29ce484222325;
    const uint64_t prime = 0x100000001b3;

    for (char c : hash_input) {
        hash ^= static_cast<uint64_t>(c);
        hash *= prime;
    }

    std::ostringstream final_hash;
    final_hash << result << std::hex << std::setfill('0') << std::setw(16) << hash;

    return final_hash.str();
}

std::string detect_extension_from_magic(const std::string& body) {
    if (body.size() < 8) return ".bin";

    const unsigned char* data = reinterpret_cast<const unsigned char*>(body.data());

    if (data[0] == 0xFF && data[1] == 0xD8) {
        return ".jpg";
    }
    if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
        return ".png";
    }
    if (data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') {
        return ".webp";
    }
    if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F') {
        return ".gif";
    }
    if (data[0] == 0x25 && data[1] == 0x50 && data[2] == 0x44 && data[3] == 0x46) {
        return ".pdf";
    }
    if ((data[0] == 'P' && data[1] == 'K') && (data[2] == 0x03 && data[3] == 0x04)) {
        return ".zip";
    }

    return ".bin";
}

std::string detect_extension_from_content_type(const std::string& content_type) {
    if (content_type == "image/jpeg") return ".jpg";
    if (content_type == "image/png") return ".png";
    if (content_type == "image/gif") return ".gif";
    if (content_type == "image/webp") return ".webp";
    if (content_type == "application/pdf") return ".pdf";
    if (content_type == "application/zip") return ".zip";
    if (content_type == "application/octet-stream") return ".bin";

    return ".bin";
}

}
