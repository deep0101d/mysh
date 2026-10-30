#include "builtins.h"

#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#include "executor.h"
#include "jobs.h"
#include "shell.h"
#include "utils.h"

extern char** environ;

namespace {

using Args = std::vector<std::string>;

// ---- cd / pwd / echo --------------------------------------------------------

int bi_cd(Shell&, const Args& a) {
    if (a.size() > 2) {
        util::error("cd", "too many arguments");
        return 1;
    }
    std::string target;
    bool print = false;
    if (a.size() == 1) {
        const char* h = getenv("HOME");
        if (!h || !*h) {
            util::error("cd", "HOME not set");
            return 1;
        }
        target = h;
    } else if (a[1] == "-") {
        const char* o = getenv("OLDPWD");
        if (!o || !*o) {
            util::error("cd", "OLDPWD not set");
            return 1;
        }
        target = o;
        print = true;
    } else {
        target = a[1];
    }

    char old[PATH_MAX];
    std::string oldpwd = getcwd(old, sizeof old) ? old : (getenv("PWD") ? getenv("PWD") : "");
    if (chdir(target.c_str()) != 0) {
        util::error("cd", target + ": " + std::strerror(errno));
        return 1;
    }
    char now[PATH_MAX];
    if (getcwd(now, sizeof now)) {
        if (!oldpwd.empty()) setenv("OLDPWD", oldpwd.c_str(), 1);
        setenv("PWD", now, 1);
        if (print) std::puts(now);
    }
    return 0;
}

int bi_pwd(Shell&, const Args&) {
    char buf[PATH_MAX];
    if (!getcwd(buf, sizeof buf)) {
        util::errno_error("pwd");
        return 1;
    }
    std::puts(buf);
    return 0;
}

int bi_echo(Shell&, const Args& a) {
    size_t i = 1;
    bool newline = true;
    while (i < a.size() && a[i] == "-n") {
        newline = false;
        ++i;
    }
    for (; i < a.size(); ++i) {
        std::fputs(a[i].c_str(), stdout);
        if (i + 1 < a.size()) std::fputc(' ', stdout);
    }
    if (newline) std::fputc('\n', stdout);
    return 0;
}

// ---- environment ------------------------------------------------------------

int bi_export(Shell&, const Args& a) {
    if (a.size() == 1) {
        std::vector<std::string> vars;
        for (char** e = environ; e && *e; ++e) vars.emplace_back(*e);
        std::sort(vars.begin(), vars.end());
        for (const std::string& v : vars) {
            size_t eq = v.find('=');
            if (eq == std::string::npos) continue;
            std::printf("export %s=\"", v.substr(0, eq).c_str());
            for (char c : v.substr(eq + 1)) {
                if (c == '"' || c == '\\' || c == '$' || c == '`') std::fputc('\\', stdout);
                std::fputc(c, stdout);
            }
            std::puts("\"");
        }
        return 0;
    }
    int status = 0;
    for (size_t i = 1; i < a.size(); ++i) {
        size_t eq = a[i].find('=');
        std::string name = a[i].substr(0, eq);
        if (!util::is_valid_name(name)) {
            util::error("export", "`" + a[i] + "': not a valid identifier");
            status = 1;
            continue;
        }
        if (eq != std::string::npos) setenv(name.c_str(), a[i].c_str() + eq + 1, 1);
    }
    return status;
}

int bi_unset(Shell&, const Args& a) {
    int status = 0;
    for (size_t i = 1; i < a.size(); ++i) {
        if (!util::is_valid_name(a[i])) {
            util::error("unset", "`" + a[i] + "': not a valid identifier");
            status = 1;
            continue;
        }
        unsetenv(a[i].c_str());
    }
    return status;
}

// ---- exit / history / source / help -----------------------------------------

int bi_exit(Shell& sh, const Args& a) {
    if (sh.interactive && !sh.warned_stopped && sh.jobs.has_stopped()) {
        std::fputs("There are stopped jobs.\n", stderr);
        sh.warned_stopped = true;  // a second `exit` really exits
        return 1;
    }
    int code = sh.last_status;
    if (a.size() > 1) {
        char* end = nullptr;
        long v = std::strtol(a[1].c_str(), &end, 10);
        if (a[1].empty() || *end != '\0') {
            util::error("exit", a[1] + ": numeric argument required");
            code = 2;
        } else {
            code = static_cast<int>(v & 0xFF);
        }
    }
    sh.exiting = true;
    sh.exit_code = code;
    return code;
}

int bi_history(Shell& sh, const Args& a) {
    if (a.size() > 1 && a[1] == "-c") {
        sh.history.clear();
        return 0;
    }
    for (size_t i = 0; i < sh.history.size(); ++i) {
        std::printf("%5zu  %s\n", i + 1, sh.history[i].c_str());
    }
    return 0;
}

int bi_source(Shell& sh, const Args& a) {
    if (a.size() < 2) {
        util::error(a[0], "filename argument required");
        return 2;
    }
    return sh.run_file(a[1], false);
}

int bi_help(Shell&, const Args&) {
    std::puts(
        "mysh builtins:\n"
        "  cd [dir|-]        change directory\n"
        "  pwd               print working directory\n"
        "  echo [-n] ...     print arguments\n"
        "  export [N=V ...]  set environment variables (no args: list them)\n"
        "  unset NAME ...    remove variables\n"
        "  history [-c]      show / clear command history\n"
        "  source FILE       run commands from FILE in this shell (also: . FILE)\n"
        "  jobs [-l]         list background and stopped jobs\n"
        "  fg [%n]           bring a job to the foreground\n"
        "  bg [%n]           continue a stopped job in the background\n"
        "  kill [-SIG] T...  send a signal to a pid or %job\n"
        "  wait [T...]       wait for background jobs\n"
        "  exit [n]          leave the shell");
    return 0;
}

// ---- job control ------------------------------------------------------------

int bi_jobs(Shell& sh, const Args& a) {
    sh.jobs.poll();
    bool lng = a.size() > 1 && a[1] == "-l";
    for (const Job& j : sh.jobs.all()) sh.jobs.print(j, stdout, lng);
    sh.jobs.remove_completed();
    return 0;
}

int bi_fg(Shell& sh, const Args& a) {
    if (!sh.interactive) {
        util::error("fg", "no job control");
        return 1;
    }
    Job* j = a.size() > 1 ? sh.jobs.parse_spec(a[1]) : sh.jobs.current();
    if (!j) {
        util::error("fg", (a.size() > 1 ? a[1] : std::string("current")) + ": no such job");
        return 1;
    }
    std::puts(j->cmd.c_str());
    std::fflush(stdout);
    return continue_in_foreground(sh, *j);
}

int bi_bg(Shell& sh, const Args& a) {
    if (!sh.interactive) {
        util::error("bg", "no job control");
        return 1;
    }
    Job* j = a.size() > 1 ? sh.jobs.parse_spec(a[1]) : sh.jobs.current();
    if (!j) {
        util::error("bg", (a.size() > 1 ? a[1] : std::string("current")) + ": no such job");
        return 1;
    }
    if (!j->stopped()) {
        util::error("bg", "job " + std::to_string(j->id) + " already in background");
        return 0;
    }
    for (Process& p : j->procs) {
        if (!p.completed) p.stopped = false;
    }
    j->foreground = false;
    j->notified = false;
    sh.jobs.touch(*j);
    signal_job(*j, SIGCONT);
    std::printf("[%d]+ %s &\n", j->id, j->cmd.c_str());
    return 0;
}

struct SigName {
    const char* name;
    int num;
};
const SigName kSignals[] = {
    {"HUP", SIGHUP},   {"INT", SIGINT},   {"QUIT", SIGQUIT}, {"KILL", SIGKILL},
    {"USR1", SIGUSR1}, {"USR2", SIGUSR2}, {"PIPE", SIGPIPE}, {"ALRM", SIGALRM},
    {"TERM", SIGTERM}, {"CONT", SIGCONT}, {"STOP", SIGSTOP}, {"TSTP", SIGTSTP},
};

int parse_signal(std::string s) {
    if (s.empty()) return -1;
    if (std::isdigit(static_cast<unsigned char>(s[0]))) return std::atoi(s.c_str());
    if (s.compare(0, 3, "SIG") == 0) s = s.substr(3);
    for (const SigName& n : kSignals) {
        if (s == n.name) return n.num;
    }
    return -1;
}

int bi_kill(Shell& sh, const Args& a) {
    int sig = SIGTERM;
    size_t i = 1;
    if (i < a.size() && a[i].size() > 1 && a[i][0] == '-') {
        std::string s = a[i] == "-s" && i + 1 < a.size() ? a[++i] : a[i].substr(1);
        sig = parse_signal(s);
        if (sig < 0) {
            util::error("kill", s + ": invalid signal specification");
            return 1;
        }
        ++i;
    }
    if (i >= a.size()) {
        std::fputs("kill: usage: kill [-SIGNAL] pid | %job ...\n", stderr);
        return 2;
    }
    int status = 0;
    for (; i < a.size(); ++i) {
        if (a[i][0] == '%') {
            Job* j = sh.jobs.parse_spec(a[i]);
            if (!j) {
                util::error("kill", a[i] + ": no such job");
                status = 1;
                continue;
            }
            bool was_stopped = j->stopped();
            signal_job(*j, sig);
            // A stopped job can't act on TERM/HUP until it is resumed.
            if (was_stopped && (sig == SIGTERM || sig == SIGHUP)) signal_job(*j, SIGCONT);
        } else {
            char* end = nullptr;
            long pid = std::strtol(a[i].c_str(), &end, 10);
            if (a[i].empty() || *end) {
                util::error("kill", a[i] + ": arguments must be process or job IDs");
                status = 1;
            } else if (kill(static_cast<pid_t>(pid), sig) != 0) {
                util::error("kill", "(" + a[i] + ") - " + std::strerror(errno));
                status = 1;
            }
        }
    }
    return status;
}

int bi_wait(Shell& sh, const Args& a) {
    if (a.size() == 1) {
        while (true) {
            Job* running = nullptr;
            for (Job& j : sh.jobs.all()) {
                if (!j.completed() && !j.stopped()) {
                    running = &j;
                    break;
                }
            }
            if (!running) break;
            if (!sh.jobs.wait_for(*running, true)) return 130;
        }
        sh.jobs.remove_completed();
        return 0;
    }
    int status = 0;
    for (size_t i = 1; i < a.size(); ++i) {
        Job* j = nullptr;
        if (a[i][0] == '%') {
            j = sh.jobs.parse_spec(a[i]);
        } else {  // a pid
            pid_t pid = static_cast<pid_t>(std::atol(a[i].c_str()));
            for (Job& cand : sh.jobs.all()) {
                for (const Process& p : cand.procs) {
                    if (p.pid == pid) j = &cand;
                }
            }
        }
        if (!j) {
            util::error("wait", a[i] + ": no such job");
            status = 127;
            continue;
        }
        if (!sh.jobs.wait_for(*j, true)) return 130;
        status = j->exit_status();
        sh.jobs.remove(j->id);
    }
    return status;
}

// ---- table ------------------------------------------------------------------

using Fn = int (*)(Shell&, const Args&);
const std::map<std::string, Fn>& table() {
    static const std::map<std::string, Fn> t = {
        {"cd", bi_cd},         {"pwd", bi_pwd},         {"echo", bi_echo},
        {"export", bi_export}, {"unset", bi_unset},     {"exit", bi_exit},
        {"history", bi_history}, {"source", bi_source}, {".", bi_source},
        {"help", bi_help},     {"jobs", bi_jobs},       {"fg", bi_fg},
        {"bg", bi_bg},         {"kill", bi_kill},       {"wait", bi_wait},
    };
    return t;
}

}  // namespace

bool is_builtin(const std::string& name) { return table().count(name) != 0; }

int run_builtin(Shell& sh, const std::vector<std::string>& argv) {
    auto it = table().find(argv[0]);
    if (it == table().end()) return 127;
    int st = it->second(sh, argv);
    std::fflush(stdout);
    return st;
}
