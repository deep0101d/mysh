// shell.h - global shell state and the read-eval loop.
#pragma once

#include <sys/types.h>
#include <termios.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

#include "jobs.h"

enum class ReadResult { Line, Eof, Interrupted };
using ReaderFn = std::function<ReadResult(const std::string& prompt, std::string& line)>;

class Shell {
public:
    Shell();

    // Interactive setup: own process group, take the terminal, signals, history.
    void init_interactive();

    int run_repl();                                // interactive prompt loop
    int run_string(const std::string& text, bool top_level = true);  // mysh -c "..."
    int run_fd(int fd, bool top_level);            // commands piped into stdin
    int run_file(const std::string& path, bool top_level);

    // $NAME lookup used by word expansion (handles $? $$ $! $0 too).
    std::string lookup_var(const std::string& name) const;

    int exit_status() const { return exiting ? exit_code : last_status; }

    // ---- state (public on purpose: builtins and the executor use it) ----
    bool interactive = false;  // job control on?
    int tty_fd = -1;           // private dup of the terminal
    pid_t pgid = 0;            // the shell's own process group
    termios tmodes{};          // terminal modes to restore after a job
    int last_status = 0;       // $?
    pid_t last_bg_pid = 0;     // $!
    bool exiting = false;
    int exit_code = 0;
    bool warned_stopped = false;
    Jobs jobs;
    std::vector<std::string> history;

private:
    int run_loop(const ReaderFn& read, bool prompting, bool top_level);
    std::string prompt() const;
    void record_history(const std::string& src);
    void load_history();
    void save_history() const;
};
