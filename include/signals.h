// signals.h - signal setup for the shell and for its children.
#pragma once

namespace sig {

// Interactive shell: ignore the job-control signals (Ctrl+Z, tty I/O from
// background, Ctrl+\) and catch SIGINT so Ctrl+C just redraws the prompt.
void init_interactive();

// Called in every forked child before exec: back to default dispositions.
void reset_child();

// Tell the SIGINT handler whether we are sitting inside readline().
void set_reading(bool reading);

// True if SIGINT arrived since the last call (clears the flag).
bool interrupted();

}  // namespace sig
