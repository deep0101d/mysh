// builtins.h - commands implemented inside the shell itself.
//
// `cd`, `exit`, `export`, ... must change the shell's own state, so they cannot
// be run in a forked child (a child's chdir would vanish when it exits).
#pragma once

#include <string>
#include <vector>

class Shell;

bool is_builtin(const std::string& name);
int run_builtin(Shell& sh, const std::vector<std::string>& argv);
