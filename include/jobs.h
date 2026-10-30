// jobs.h - the job table.
//
// A "job" is one pipeline (or one backgrounded and-or list). All processes of
// a pipeline share a process group (pgid) so the terminal and signals such as
// Ctrl+C / Ctrl+Z can be directed at the whole pipeline at once.
#pragma once

#include <sys/types.h>
#include <termios.h>

#include <cstdio>
#include <list>
#include <string>
#include <vector>

struct Process {
    pid_t pid = 0;
    bool completed = false;
    bool stopped = false;
    int status = 0;  // raw status from waitpid
};

struct Job {
    int id = 0;
    pid_t pgid = 0;  // 0 when job control is off (scripts)
    std::vector<Process> procs;
    std::string cmd;
    bool foreground = false;
    bool notified = false;  // already told the user it stopped
    long seq = 0;           // recency; highest = "current job" (%+)
    termios tmodes{};       // terminal modes saved when the job was stopped
    bool has_tmodes = false;

    bool completed() const;
    bool stopped() const;
    int exit_status() const;  // shell-style status of the last process
};

// Sends sig to every process of the job.
void signal_job(const Job& job, int sig);

class Jobs {
public:
    Job& add(pid_t pgid, const std::vector<pid_t>& pids, const std::string& cmd, bool foreground);
    void remove(int id);
    void clear() { jobs_.clear(); }
    void touch(Job& j) { j.seq = ++counter_; }

    Job* find(int id);
    Job* current();
    Job* previous();
    // Accepts "%n", "%%", "%+", "%-", "%text", or a bare number. nullptr if none matches.
    Job* parse_spec(const std::string& spec);

    std::list<Job>& all() { return jobs_; }
    bool has_stopped() const;

    // Non-blocking: collect status changes of any child.
    void poll();
    // Blocking: wait until the job has completed or every live process is stopped.
    // If interruptible, a caught SIGINT ends the wait early (returns false then).
    bool wait_for(Job& job, bool interruptible = false);
    // poll(), then report/discard finished background jobs and report newly stopped ones.
    void notify(bool print);
    void remove_completed();

    void print(const Job& j, FILE* out, bool long_format) const;
    static std::string state_of(const Job& j);

private:
    void update(pid_t pid, int status);
    char marker(const Job& j) const;

    std::list<Job> jobs_;  // std::list: references to jobs stay valid
    long counter_ = 0;
};
