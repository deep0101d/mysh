// mysh - a small Unix shell.
//
//   mysh                  interactive shell (when stdin is a terminal)
//   mysh script.sh        run a script
//   mysh -c "cmd; cmd"    run a command string
//   echo "cmd" | mysh     read commands from stdin
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "shell.h"

static void usage() {
    std::puts("usage: mysh [-c command | script-file]");
}

int main(int argc, char** argv) {
    Shell sh;

    if (argc >= 2 && (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0)) {
        usage();
        return 0;
    }

    if (argc >= 2 && std::strcmp(argv[1], "-c") == 0) {
        if (argc < 3) {
            std::fputs("mysh: -c: option requires an argument\n", stderr);
            return 2;
        }
        return sh.run_string(argv[2]);
    }

    if (argc >= 2) {
        return sh.run_file(argv[1], true);
    }

    if (isatty(STDIN_FILENO) && isatty(STDERR_FILENO)) {
        sh.init_interactive();
        return sh.run_repl();
    }
    return sh.run_fd(STDIN_FILENO, true);
}
