#include "executor.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "builtins.h"
#include "jobs.h"
#include "lexer.h"
#include "shell.h"
#include "signals.h"
#include "utils.h"

namespace {

// Flush only the output streams. fflush(NULL) would also "sync" the script
// file we are reading, which on some libcs rewinds the file offset that a
// forked child shares with the parent, making the parent re-read a line.
void flush_output() {
    std::fflush(stdout);
    std::fflush(stderr);
}

// ---------------------------------------------------------------------------
// Preparing a command: expand its words (at the moment it is about to run).
// ---------------------------------------------------------------------------

struct ExpandedRedir {
    Redirection::Type type;
    int fd;
    std::string target;
    int dup_fd;
};

struct Prepared {
    std::vector<std::string> argv;
    std::vector<std::pair<std::string, std::string>> assigns;  // NAME, value
    std::vector<ExpandedRedir> redirs;
    bool pure_assign = false;  // the command is only NAME=value words
};

Prepared prepare(Shell& sh, const Command& c) {
    LookupFn lookup = [&sh](const std::string& name) { return sh.lookup_var(name); };
    Prepared p;
    p.pure_assign = c.n_assign > 0 && c.n_assign == c.words.size();
    for (size_t i = 0; i < c.words.size(); ++i) {
        if (i < c.n_assign) {
            std::string s = expand_single(c.words[i], lookup);
            size_t eq = s.find('=');
            p.assigns.emplace_back(s.substr(0, eq), s.substr(eq + 1));
        } else {
            std::vector<std::string> v = expand_word(c.words[i], lookup);
            p.argv.insert(p.argv.end(), v.begin(), v.end());
        }
    }
    for (const Redirection& r : c.redirs) {
        ExpandedRedir er;
        er.type = r.type;
        er.fd = r.fd;
        er.dup_fd = r.dup_fd;
        if (r.type != Redirection::Type::Dup) er.target = expand_single(r.target, lookup);
        p.redirs.push_back(std::move(er));
    }
    return p;
}

// ---------------------------------------------------------------------------
// Redirections
// ---------------------------------------------------------------------------

struct SavedFd {
    int fd;
    int saved;  // -1 if fd was not open
};

// Applies redirections in order. If `saved` is non-null the original fds are
// saved so the caller can restore them afterwards (builtins run in the shell).
bool apply_redirs(const std::vector<ExpandedRedir>& redirs, std::vector<SavedFd>* saved) {
    for (const ExpandedRedir& r : redirs) {
        if (saved) {
            bool have = false;
            for (const SavedFd& s : *saved) {
                if (s.fd == r.fd) have = true;
            }
            if (!have) saved->push_back({r.fd, fcntl(r.fd, F_DUPFD_CLOEXEC, 10)});
        }
        if (r.type == Redirection::Type::Dup) {
            if (fcntl(r.dup_fd, F_GETFD) < 0) {
                util::error(std::to_string(r.dup_fd), "Bad file descriptor");
                return false;
            }
            if (dup2(r.dup_fd, r.fd) < 0) {
                util::errno_error("dup2");
                return false;
            }
            continue;
        }
        int flags = O_RDONLY;
        if (r.type == Redirection::Type::Out) flags = O_WRONLY | O_CREAT | O_TRUNC;
        if (r.type == Redirection::Type::Append) flags = O_WRONLY | O_CREAT | O_APPEND;
        int f = open(r.target.c_str(), flags, 0666);
        if (f < 0) {
            util::errno_error(r.target);
            return false;
        }
        if (f != r.fd) {
            if (dup2(f, r.fd) < 0) {
                util::errno_error("dup2");
                close(f);
                return false;
            }
            close(f);
        }
    }
    return true;
}

void restore_fds(std::vector<SavedFd>& saved) {
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
        if (it->saved >= 0) {
            dup2(it->saved, it->fd);
            close(it->saved);
        } else {
            close(it->fd);
        }
    }
    saved.clear();
}

// ---------------------------------------------------------------------------
// Child side of fork()
// ---------------------------------------------------------------------------

[[noreturn]] void child_exec(Shell& sh, const Prepared& p) {
    for (const auto& a : p.assigns) setenv(a.first.c_str(), a.second.c_str(), 1);
    if (p.argv.empty()) _exit(0);

    if (is_builtin(p.argv[0])) {
        int st = run_builtin(sh, p.argv);
        flush_output();
        _exit(st);
    }

    std::vector<char*> args;
    for (const std::string& a : p.argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    execvp(args[0], args.data());

    int e = errno;
    if (e == ENOENT) {
        if (p.argv[0].find('/') == std::string::npos) util::error(p.argv[0], "command not found");
        else util::error(p.argv[0], "No such file or directory");
        _exit(127);
    }
    util::error(p.argv[0], std::strerror(e));
    _exit(126);
}

// ---------------------------------------------------------------------------
// Foreground handling
// ---------------------------------------------------------------------------

// Waits for `job` (already running in the foreground, or resumed when
// `resume` is set), then takes the terminal back. Returns the exit status.
int finish_foreground(Shell& sh, Job& job, bool resume) {
    if (sh.interactive) {
        tcsetpgrp(sh.tty_fd, job.pgid);  // job gets the terminal (Ctrl+C / Ctrl+Z go to it)
        if (resume && job.has_tmodes) tcsetattr(sh.tty_fd, TCSADRAIN, &job.tmodes);
    }
    if (resume) {
        for (Process& p : job.procs) {
            if (!p.completed) p.stopped = false;
        }
        signal_job(job, SIGCONT);
    }

    sh.jobs.wait_for(job);

    if (sh.interactive) {
        tcsetpgrp(sh.tty_fd, sh.pgid);  // shell takes the terminal back
        if (job.stopped()) {
            tcgetattr(sh.tty_fd, &job.tmodes);
            job.has_tmodes = true;
        }
        tcsetattr(sh.tty_fd, TCSADRAIN, &sh.tmodes);
    }

    if (job.completed()) {
        int status = job.exit_status();
        int raw = job.procs.back().status;
        if (WIFSIGNALED(raw)) {
            int s = WTERMSIG(raw);
            if (s == SIGINT) {
                if (sh.interactive) std::fputc('\n', stderr);
            } else if (s != SIGPIPE) {
                std::fprintf(stderr, "%s%s\n", strsignal(s), WCOREDUMP(raw) ? " (core dumped)" : "");
            }
        }
        sh.jobs.remove(job.id);
        return status;
    }

    // Stopped (Ctrl+Z): leave it in the table as a background job.
    job.foreground = false;
    job.notified = true;
    sh.jobs.touch(job);
    std::fputc('\n', stdout);
    sh.jobs.print(job, stdout, false);
    std::fflush(stdout);
    int sigstop = SIGTSTP;
    for (const Process& p : job.procs) {
        if (WIFSTOPPED(p.status)) sigstop = WSTOPSIG(p.status);
    }
    return 128 + sigstop;
}

int run_andor(Shell& sh, const AndOrList& list);

// ---------------------------------------------------------------------------
// Running one pipeline
// ---------------------------------------------------------------------------

int run_pipeline(Shell& sh, const Pipeline& pl, bool background) {
    std::vector<Prepared> preps;
    for (const Command& c : pl.cmds) preps.push_back(prepare(sh, c));
    const size_t n = preps.size();

    // A lone command in the foreground may run inside the shell process itself.
    if (n == 1 && !background) {
        Prepared& p = preps[0];
        if (p.pure_assign) {
            for (const auto& a : p.assigns) setenv(a.first.c_str(), a.second.c_str(), 1);
            return 0;
        }
        if (p.argv.empty()) {  // e.g. `> file` : just perform the redirections
            std::vector<SavedFd> saved;
            bool ok = apply_redirs(p.redirs, &saved);
            restore_fds(saved);
            return ok ? 0 : 1;
        }
        if (is_builtin(p.argv[0])) {
            std::vector<SavedFd> saved;
            if (!apply_redirs(p.redirs, &saved)) {
                restore_fds(saved);
                return 1;
            }
            int st = run_builtin(sh, p.argv);
            std::fflush(stdout);
            restore_fds(saved);
            return st;
        }
    }

    flush_output();  // don't let buffered output be duplicated by fork()

    std::vector<pid_t> pids;
    pid_t pgid = 0;
    int in_fd = STDIN_FILENO;

    for (size_t i = 0; i < n; ++i) {
        const bool last = (i + 1 == n);
        int fds[2] = {-1, -1};
        if (!last && pipe(fds) < 0) {
            util::errno_error("pipe");
            if (in_fd != STDIN_FILENO) close(in_fd);
            break;
        }

        pid_t pid = fork();
        if (pid < 0) {
            util::errno_error("fork");
            if (in_fd != STDIN_FILENO) close(in_fd);
            if (!last) {
                close(fds[0]);
                close(fds[1]);
            }
            break;
        }

        if (pid == 0) {
            // ---- child ----
            if (sh.interactive) {
                pid_t me = getpid();
                if (pgid == 0) pgid = me;  // first process leads the group
                setpgid(me, pgid);
                if (!background) tcsetpgrp(sh.tty_fd, pgid);
            }
            sig::reset_child();

            if (background && !sh.interactive && i == 0) {
                int dn = open("/dev/null", O_RDONLY);  // background jobs don't read the script
                if (dn >= 0) {
                    dup2(dn, STDIN_FILENO);
                    close(dn);
                }
            }
            if (in_fd != STDIN_FILENO) {
                dup2(in_fd, STDIN_FILENO);
                close(in_fd);
            }
            if (!last) {
                close(fds[0]);
                dup2(fds[1], STDOUT_FILENO);
                close(fds[1]);
            }
            if (!apply_redirs(preps[i].redirs, nullptr)) _exit(1);  // explicit redirs win over pipes
            child_exec(sh, preps[i]);
        }

        // ---- parent ----
        if (sh.interactive) {
            if (pgid == 0) pgid = pid;
            setpgid(pid, pgid);  // also done by the child: whoever runs first wins, no race
        }
        pids.push_back(pid);
        if (in_fd != STDIN_FILENO) close(in_fd);
        if (!last) {
            close(fds[1]);
            in_fd = fds[0];
        }
    }

    if (pids.empty()) return 1;

    Job& job = sh.jobs.add(sh.interactive ? pgid : 0, pids, pl.text, !background);
    if (background) {
        job.foreground = false;
        sh.last_bg_pid = pids.back();
        if (sh.interactive) std::printf("[%d] %d\n", job.id, static_cast<int>(pids.back()));
        std::fflush(stdout);
        return 0;
    }
    return finish_foreground(sh, job, false);
}

// `a && b &` : the whole and-or list runs in a background subshell.
int run_background_list(Shell& sh, const AndOrList& list) {
    flush_output();
    pid_t pid = fork();
    if (pid < 0) {
        util::errno_error("fork");
        return 1;
    }
    if (pid == 0) {
        if (sh.interactive) setpgid(0, 0);
        sig::reset_child();
        int dn = open("/dev/null", O_RDONLY);
        if (dn >= 0) {
            dup2(dn, STDIN_FILENO);
            close(dn);
        }
        sh.interactive = false;  // the subshell does no terminal juggling
        sh.jobs.clear();
        AndOrList fg = list;
        fg.background = false;
        int st = run_andor(sh, fg);
        flush_output();
        _exit(sh.exiting ? sh.exit_code : st);
    }
    if (sh.interactive) setpgid(pid, pid);
    Job& job = sh.jobs.add(sh.interactive ? pid : 0, {pid}, list.text, false);
    sh.last_bg_pid = pid;
    if (sh.interactive) std::printf("[%d] %d\n", job.id, static_cast<int>(pid));
    std::fflush(stdout);
    return 0;
}

int run_andor(Shell& sh, const AndOrList& list) {
    if (list.background) {
        if (list.items.size() == 1) return run_pipeline(sh, list.items[0].pipeline, true);
        return run_background_list(sh, list);
    }
    int status = sh.last_status;
    for (size_t i = 0; i < list.items.size(); ++i) {
        if (sh.exiting) break;
        const AndOrItem& item = list.items[i];
        // Left-associative: `a || b && c` == `(a || b) && c`; a skipped item
        // leaves the status unchanged.
        if (i > 0) {
            if (item.conn == Connector::And && status != 0) continue;
            if (item.conn == Connector::Or && status == 0) continue;
        }
        status = run_pipeline(sh, item.pipeline, false);
        sh.last_status = status;
    }
    return status;
}

}  // namespace

int execute(Shell& sh, const CommandLine& line) {
    int status = sh.last_status;
    for (const AndOrList& list : line.lists) {
        if (sh.exiting) break;
        status = run_andor(sh, list);
        sh.last_status = status;
    }
    return status;
}

int continue_in_foreground(Shell& sh, Job& job) {
    job.foreground = true;
    job.notified = false;
    sh.jobs.touch(job);
    return finish_foreground(sh, job, true);
}
