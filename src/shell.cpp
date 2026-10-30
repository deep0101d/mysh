#include "shell.h"

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef HAVE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#endif

#include "executor.h"
#include "parser.h"
#include "signals.h"
#include "utils.h"

namespace {

const size_t kMaxHistory = 1000;

std::string history_path() {
    const char* h = std::getenv("HOME");
    return (h && *h) ? std::string(h) + "/.mysh_history" : std::string();
}

// Reads one line from the terminal (readline if available, plain getline otherwise).
ReadResult read_interactive(const std::string& prompt, std::string& out) {
#ifdef HAVE_READLINE
    sig::set_reading(true);
    char* l = readline(prompt.c_str());
    sig::set_reading(false);
    if (!l) return ReadResult::Eof;
    out = l;
    std::free(l);
    return ReadResult::Line;
#else
    std::fputs(prompt.c_str(), stdout);
    std::fflush(stdout);
    char* buf = nullptr;
    size_t cap = 0;
    errno = 0;
    ssize_t n = getline(&buf, &cap, stdin);
    if (n < 0) {
        std::free(buf);
        if (errno == EINTR) {  // Ctrl+C at the prompt
            clearerr(stdin);
            return ReadResult::Interrupted;
        }
        return ReadResult::Eof;
    }
    out.assign(buf);
    std::free(buf);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return ReadResult::Line;
#endif
}

// Reads one line straight from a file descriptor, one byte at a time, so we
// never consume more input than the current line. (Buffered stdio could read
// ahead, and then a child such as `cat` or a stdio rewind on exit would see or
// disturb the shared file offset.) Used for scripts piped into stdin.
ReadResult read_line_fd(int fd, std::string& out) {
    out.clear();
    bool got_any = false;
    while (true) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return got_any ? ReadResult::Line : ReadResult::Eof;
        }
        if (n == 0) return got_any ? ReadResult::Line : ReadResult::Eof;
        got_any = true;
        if (c == '\n') break;
        out += c;
    }
    while (!out.empty() && out.back() == '\r') out.pop_back();
    return ReadResult::Line;
}

}  // namespace

Shell::Shell() {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof cwd)) setenv("PWD", cwd, 1);
}

std::string Shell::lookup_var(const std::string& name) const {
    if (name == "?") return std::to_string(last_status);
    if (name == "$") return std::to_string(static_cast<long>(getpid()));
    if (name == "!") return last_bg_pid ? std::to_string(static_cast<long>(last_bg_pid)) : "";
    if (name == "0") return "mysh";
    if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0]))) return "";
    const char* v = std::getenv(name.c_str());
    return v ? v : "";
}

// ---------------------------------------------------------------------------
// Interactive setup (job control)
// ---------------------------------------------------------------------------

void Shell::init_interactive() {
    tty_fd = fcntl(STDIN_FILENO, F_DUPFD_CLOEXEC, 10);  // private copy, immune to `< file`
    if (tty_fd < 0) tty_fd = STDIN_FILENO;

    // If we were started in the background, wait until we are in the foreground.
    while (tcgetpgrp(tty_fd) != (pgid = getpgrp())) kill(-pgid, SIGTTIN);

    sig::init_interactive();

    // Put the shell in its own process group and make that group the
    // terminal's foreground group.
    pgid = getpid();
    if (getpgrp() != pgid && setpgid(pgid, pgid) < 0) {
        util::errno_error("setpgid");
        _exit(1);
    }
    tcsetpgrp(tty_fd, pgid);
    tcgetattr(tty_fd, &tmodes);  // remembered, and restored after every job

    interactive = true;
    load_history();
}

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

void Shell::load_history() {
    std::string path = history_path();
    if (path.empty()) return;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        history.push_back(line);
#ifdef HAVE_READLINE
        add_history(line.c_str());
#endif
    }
}

void Shell::save_history() const {
    std::string path = history_path();
    if (path.empty()) return;
    std::ofstream out(path, std::ios::trunc);
    size_t start = history.size() > kMaxHistory ? history.size() - kMaxHistory : 0;
    for (size_t i = start; i < history.size(); ++i) out << history[i] << '\n';
}

void Shell::record_history(const std::string& src) {
    std::string line = src;
    for (char& c : line) {
        if (c == '\n') c = ' ';
    }
    if (!history.empty() && history.back() == line) return;
    history.push_back(line);
#ifdef HAVE_READLINE
    add_history(line.c_str());
#endif
}

// ---------------------------------------------------------------------------
// The read-eval loop
// ---------------------------------------------------------------------------

std::string Shell::prompt() const {
    char buf[PATH_MAX];
    std::string cwd = getcwd(buf, sizeof buf) ? util::shorten_home(buf) : "?";
#ifdef HAVE_READLINE
    // \001 .. \002 tell readline the enclosed bytes take no screen width.
    return "\001\033[1;32m\002mysh\001\033[0m\002:\001\033[1;34m\002" + cwd +
           "\001\033[0m\002$ ";
#else
    return "mysh:" + cwd + "$ ";
#endif
}

// read:      where lines come from
// prompting: show prompts, keep history, print job notifications (the REPL)
// top_level: false for `source`d files
int Shell::run_loop(const ReaderFn& read, bool prompting, bool top_level) {
    std::string pending;  // an unfinished command (open quote, trailing '|' ...)

    while (!exiting) {
        if (prompting) jobs.notify(true);
        else if (top_level) jobs.notify(false);  // reap silently in scripts

        std::string line;
        ReadResult rr = read(pending.empty() ? (prompting ? prompt() : "") : "> ", line);
        if (rr == ReadResult::Eof) {
            if (!pending.empty()) {
                std::fputs("mysh: syntax error: unexpected end of file\n", stderr);
                last_status = 2;
            } else if (prompting) {
                std::puts("exit");
            }
            break;
        }
        if (rr == ReadResult::Interrupted) {  // Ctrl+C: abandon the half-typed command
            pending.clear();
            continue;
        }

        if (pending.empty() && util::trim(line).empty()) continue;
        std::string src = pending.empty() ? line : pending + "\n" + line;

        ParseResult pr = parse(src);
        if (pr.status == ParseStatus::Incomplete) {  // ask for another line
            pending = src;
            continue;
        }
        pending.clear();
        if (prompting) record_history(src);

        if (pr.status == ParseStatus::Error) {
            std::fprintf(stderr, "mysh: %s\n", pr.error.c_str());
            last_status = 2;
            if (!prompting) return 2;  // scripts stop at a syntax error
            continue;
        }
        execute(*this, pr.line);
    }
    return exit_status();
}

int Shell::run_repl() {
    int rc = run_loop(read_interactive, true, true);
    save_history();
    // Like bash: hang up on our jobs when the shell exits.
    for (Job& j : jobs.all()) {
        if (!j.completed()) {
            signal_job(j, SIGHUP);
            if (j.stopped()) signal_job(j, SIGCONT);
        }
    }
    return rc;
}

int Shell::run_fd(int fd, bool top_level) {
    ReaderFn rd = [fd](const std::string&, std::string& out) { return read_line_fd(fd, out); };
    return run_loop(rd, false, top_level);
}

int Shell::run_string(const std::string& text, bool top_level) {
    std::istringstream in(text);
    ReaderFn rd = [&in](const std::string&, std::string& out) {
        if (!std::getline(in, out)) return ReadResult::Eof;
        return ReadResult::Line;
    };
    return run_loop(rd, false, top_level);
}

int Shell::run_file(const std::string& path, bool top_level) {
    // Read the whole file first: no descriptor stays open that a forked child
    // could share with us.
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        util::error(path, std::strerror(errno));
        return 127;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return run_string(buf.str(), top_level);
}
