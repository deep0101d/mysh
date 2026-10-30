#include "utils.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace util {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool is_valid_name(const std::string& s) {
    if (s.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
    for (char c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    }
    return true;
}

void error(const std::string& who, const std::string& msg) {
    std::fprintf(stderr, "mysh: %s: %s\n", who.c_str(), msg.c_str());
}

void errno_error(const std::string& who) {
    error(who, std::strerror(errno));
}

std::string shorten_home(const std::string& path) {
    const char* h = std::getenv("HOME");
    if (h && *h) {
        std::string home = h;
        if (path == home) return "~";
        if (path.size() > home.size() && path.compare(0, home.size(), home) == 0 &&
            path[home.size()] == '/') {
            return "~" + path.substr(home.size());
        }
    }
    return path;
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

}  // namespace util
