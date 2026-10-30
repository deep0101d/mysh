// executor.h - runs parsed command lines: fork/exec, pipes, redirection, jobs.
#pragma once

#include "parser.h"

class Shell;
struct Job;

// Runs every and-or list of the line; returns the status of the last one.
int execute(Shell& sh, const CommandLine& line);

// fg: give the terminal to a (stopped or background) job and wait for it.
int continue_in_foreground(Shell& sh, Job& job);
