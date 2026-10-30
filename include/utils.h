// utils.h - small helpers shared by every module.
#pragma once

#include <string>
#include <vector>

namespace util {

std::string trim(const std::string& s);

// [A-Za-z_][A-Za-z0-9_]*
bool is_valid_name(const std::string& s);

// Prints "mysh: <who>: <msg>" to stderr.
void error(const std::string& who, const std::string& msg);

// Same, with msg = strerror(errno).
void errno_error(const std::string& who);

// Replaces a leading $HOME with "~" (for the prompt).
std::string shorten_home(const std::string& path);

std::string join(const std::vector<std::string>& parts, const std::string& sep);

}  // namespace util
