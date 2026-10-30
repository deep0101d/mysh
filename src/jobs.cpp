#include "jobs.h"

#include <signal.h>
#include <sys/wait.h>

#include <cerrno>
#include <cstring>

#include "signals.h"

namespace {

int status_to_code(int st) {
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    if (WIFSTOPPED(st)) return 128 + WSTOPSIG(st);
    return 0;
}

}  // namespace

// ---- Job ------------------------------------------------------------------

bool Job::completed() const {
    for (const Process& p : procs) {
        if (!p.completed) return false;
    }
    return true;
}

bool Job::stopped() const {
    if (completed()) return false;
    for (const Process& p : procs) {
        if (!p.completed && !p.stopped) return false;
    }
    return true;
}

int Job::exit_status() const {
    return procs.empty() ? 0 : status_to_code(procs.back().status);
}

void signal_job(const Job& job, int sig) {
    if (job.pgid > 0) {
        killpg(job.pgid, sig);
    } else {
        for (const Process& p : job.procs) {
            if (!p.completed) kill(p.pid, sig);
        }
    }
}

// ---- Jobs -----------------------------------------------------------------

Job& Jobs::add(pid_t pgid, const std::vector<pid_t>& pids, const std::string& cmd,
               bool foreground) {
    int id = 1;
    for (const Job& j : jobs_) {
        if (j.id >= id) id = j.id + 1;
    }
    jobs_.emplace_back();
    Job& j = jobs_.back();
    j.id = id;
    j.pgid = pgid;
    for (pid_t p : pids) {
        Process pr;
        pr.pid = p;
        j.procs.push_back(pr);
    }
    j.cmd = cmd;
    j.foreground = foreground;
    j.seq = ++counter_;
    return j;
}

void Jobs::remove(int id) {
    for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
        if (it->id == id) {
            jobs_.erase(it);
            return;
        }
    }
}

Job* Jobs::find(int id) {
    for (Job& j : jobs_) {
        if (j.id == id) return &j;
    }
    return nullptr;
}

Job* Jobs::current() {
    Job* best = nullptr;
    for (Job& j : jobs_) {
        if (!best || j.seq > best->seq) best = &j;
    }
    return best;
}

Job* Jobs::previous() {
    Job* cur = current();
    Job* best = nullptr;
    for (Job& j : jobs_) {
        if (&j == cur) continue;
        if (!best || j.seq > best->seq) best = &j;
    }
    return best;
}

Job* Jobs::parse_spec(const std::string& spec) {
    if (spec.empty() || spec == "%" || spec == "%%" || spec == "%+") return current();
    if (spec == "%-") return previous();
    std::string body = spec[0] == '%' ? spec.substr(1) : spec;
    bool digits = !body.empty();
    for (char c : body) {
        if (c < '0' || c > '9') digits = false;
    }
    if (digits) return find(std::atoi(body.c_str()));
    if (spec[0] == '%') {  // %text: job whose command starts with text
        for (Job& j : jobs_) {
            if (j.cmd.compare(0, body.size(), body) == 0) return &j;
        }
    }
    return nullptr;
}

bool Jobs::has_stopped() const {
    for (const Job& j : jobs_) {
        if (j.stopped()) return true;
    }
    return false;
}

void Jobs::update(pid_t pid, int status) {
    for (Job& j : jobs_) {
        for (Process& p : j.procs) {
            if (p.pid != pid) continue;
            if (WIFSTOPPED(status)) {
                p.stopped = true;
                p.status = status;
            } else if (WIFCONTINUED(status)) {
                p.stopped = false;
            } else {
                p.completed = true;
                p.stopped = false;
                p.status = status;
            }
            return;
        }
    }
}

void Jobs::poll() {
    while (true) {
        int st = 0;
        pid_t pid = waitpid(-1, &st, WNOHANG | WUNTRACED | WCONTINUED);
        if (pid <= 0) break;
        update(pid, st);
    }
}

bool Jobs::wait_for(Job& job, bool interruptible) {
    while (!job.completed() && !job.stopped()) {
        int st = 0;
        pid_t pid = waitpid(-1, &st, WUNTRACED);
        if (pid > 0) {
            update(pid, st);
            continue;
        }
        if (errno == EINTR) {
            if (interruptible && sig::interrupted()) return false;
            continue;
        }
        // ECHILD (or anything else): nothing left to wait for.
        for (Process& p : job.procs) {
            if (!p.completed) {
                p.completed = true;
                p.stopped = false;
            }
        }
    }
    return true;
}

char Jobs::marker(const Job& j) const {
    const Job* cur = nullptr;
    const Job* prev = nullptr;
    for (const Job& x : jobs_) {
        if (!cur || x.seq > cur->seq) {
            prev = cur;
            cur = &x;
        } else if (!prev || x.seq > prev->seq) {
            prev = &x;
        }
    }
    if (&j == cur) return '+';
    if (&j == prev) return '-';
    return ' ';
}

std::string Jobs::state_of(const Job& j) {
    if (j.completed()) {
        int st = j.procs.back().status;
        if (WIFEXITED(st)) {
            int code = WEXITSTATUS(st);
            return code == 0 ? "Done" : "Exit " + std::to_string(code);
        }
        if (WIFSIGNALED(st)) return strsignal(WTERMSIG(st));
        return "Done";
    }
    if (j.stopped()) return "Stopped";
    return "Running";
}

void Jobs::print(const Job& j, FILE* out, bool long_format) const {
    std::string state = state_of(j);
    std::string cmd = j.cmd;
    if (state == "Running") cmd += " &";
    if (long_format) {
        std::fprintf(out, "[%d]%c %d %-24s%s\n", j.id, marker(j), static_cast<int>(j.procs[0].pid),
                     state.c_str(), cmd.c_str());
    } else {
        std::fprintf(out, "[%d]%c  %-24s%s\n", j.id, marker(j), state.c_str(), cmd.c_str());
    }
}

void Jobs::notify(bool print_it) {
    poll();
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        Job& j = *it;
        if (j.foreground) {
            ++it;
        } else if (j.completed()) {
            if (print_it) print(j, stdout, false);
            it = jobs_.erase(it);
        } else {
            if (j.stopped()) {
                if (!j.notified) {
                    if (print_it) print(j, stdout, false);
                    j.notified = true;
                }
            } else {
                j.notified = false;
            }
            ++it;
        }
    }
    std::fflush(stdout);
}

void Jobs::remove_completed() {
    for (auto it = jobs_.begin(); it != jobs_.end();) {
        if (it->completed()) it = jobs_.erase(it);
        else ++it;
    }
}
