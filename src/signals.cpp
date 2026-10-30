#include "signals.h"

#include <signal.h>
#include <unistd.h>

#include <cstdio>

#ifdef HAVE_READLINE
#include <readline/readline.h>
#endif

namespace {

volatile sig_atomic_t g_sigint = 0;
volatile sig_atomic_t g_reading = 0;

void on_sigint(int) {
    g_sigint = 1;
    const char nl = '\n';
    ssize_t r = write(STDOUT_FILENO, &nl, 1);
    (void)r;
#ifdef HAVE_READLINE
    if (g_reading) {
        // Throw away the half-typed line and draw a fresh prompt.
        rl_on_new_line();
        rl_replace_line("", 0);
        rl_redisplay();
    }
#endif
}

}  // namespace

namespace sig {

void init_interactive() {
    struct sigaction sa;
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // no SA_RESTART: a blocked read()/waitpid() returns EINTR
    sigaction(SIGINT, &sa, nullptr);

    signal(SIGQUIT, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
}

void reset_child() {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    signal(SIGTSTP, SIG_DFL);
    signal(SIGTTIN, SIG_DFL);
    signal(SIGTTOU, SIG_DFL);
    signal(SIGPIPE, SIG_DFL);
}

void set_reading(bool reading) { g_reading = reading ? 1 : 0; }

bool interrupted() {
    bool was = g_sigint != 0;
    g_sigint = 0;
    return was;
}

}  // namespace sig
