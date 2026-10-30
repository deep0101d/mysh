# mysh design notes

How a line travels through the shell:

```
 readline / getline
        │  "ls -l | grep foo > out &"
        ▼
   lexer.cpp   ──►  tokens   (WORD, |, &&, >, ...)        quotes handled here
        ▼
   parser.cpp  ──►  CommandLine → AndOrList → Pipeline → Command
        ▼
   executor.cpp     expand words ($VAR, globs)  →  fork / pipe / dup2 / exec
        ▼
   jobs.cpp         job table, waitpid, notifications
```

`shell.cpp` owns the loop and global state, `builtins.cpp` has the commands that must
run inside the shell process, `signals.cpp` sets up signal handling.

---

## 1. The main loop (`shell.cpp`)

```
loop:
    report finished/stopped background jobs
    read a line  (prompt "mysh:~/dir$ ")
    parse it
        Incomplete  (open quote, trailing | or &&)  → prompt "> " and append the next line
        Error       → print message, $? = 2
        Ok          → execute()
```

The same loop serves four input sources through a small `ReaderFn`: readline (terminal),
a string (`-c` and script files), and unbuffered `read(2)` on stdin (piped scripts).

## 2. Lexer (`lexer.cpp`)

A hand-written state machine, one pass over the characters. Rules:

* Blanks separate words. `| & ; < >` are operators, even without spaces (`a|b`).
* `'...'` keeps everything literally. `"..."` keeps everything except `$`, `\`.
  `\x` escapes one character.
* `[n]>`, `[n]>>`, `[n]<`, `[n]>&m` are lexed as one redirection token, so `2>&1` works.
* `#` starts a comment; `~` at the start of a word means `$HOME`.

**Key design decision: a word is a list of pieces, not a string.**
`"a $X"b` becomes `[Literal "a " (quoted), Var X (quoted), Literal "b" (unquoted)]`.
We remember *which parts were quoted*, because that decides what expansion may do later
(split on spaces? treat `*` as a wildcard?). Expansion is **not** done in the lexer,
because it must see up-to-date values: in `false; echo $?` the `$?` only has its value
after `false` ran.

### Expansion (`expand_word`)
1. substitute variables;
2. **field splitting** — an *unquoted* `$X` holding `a  b` becomes two arguments, a quoted one stays one;
3. **globbing** — unquoted `* ? [` are expanded with `glob(3)`; no match keeps the pattern as typed;
4. an unquoted variable that expands to nothing disappears entirely (`echo $NOPE` → no argument),
   but `""` stays as one empty argument.

## 3. Parser (`parser.cpp`)

Recursive descent for a tiny grammar:

```
line     := list ( (';' | '&' | newline) list )*
list     := pipeline ( ('&&' | '||') pipeline )*
pipeline := command ( '|' command )*
command  := ( WORD | redirection )+
```

`&&`/`||` bind weaker than `|`, and `;` weakest. A trailing `&` marks the whole `list`
as a background job. Leading `NAME=value` words are tagged as assignments.
Running out of tokens after `|`, `&&`, `||` or inside a quote returns `Incomplete`
(that is how multi-line input works) instead of an error.

## 4. Executor (`executor.cpp`) — the core of the project

### Running one pipeline

For `a | b | c`:

```
for each command i:
    if not last:  pipe(fds)
    fork()
      child:  put myself in the pipeline's process group
              dup2(previous read end → stdin), dup2(fds[1] → stdout)
              apply explicit redirections (these win over the pipe)
              reset signals to default, then execvp()
      parent: close fds it no longer needs
              remember the read end for the next command
wait for the whole job
```

**Closing unused pipe ends is essential**: if the parent (or a sibling) keeps a write end
open, the reader never sees EOF and `a | b` hangs forever.

### Redirections
`apply_redirs` handles them left to right with `open` + `dup2`, so `> f 2>&1` (stderr to the
file) differs from `2>&1 > f` (stderr to the old stdout), exactly like a real shell.
Builtins that run inside the shell (e.g. `echo hi > f`) first *save* the original fds
(`fcntl(F_DUPFD_CLOEXEC)`) and *restore* them afterwards.

### Why builtins run in the shell, not in a child
`cd` changes the shell's own working directory and `export` its environment. A forked child
would change its own copy and then exit, so nothing would happen. Therefore a lone
foreground builtin runs in-process. Inside a pipeline (`cd /tmp | cat`) it runs in the child,
which is also what bash does.

### `&&`, `||`, `;`
Evaluated left to right with `$?`: `&&` skips its right side if the status is non-zero, `||`
skips if it is zero, and a skipped item keeps the previous status (so `a || b && c` is
`(a || b) && c`).

### Exit codes
`127` command not found, `126` found but not executable, `128+N` killed by signal N.

## 5. Job control (`jobs.cpp`, `executor.cpp`, `signals.cpp`)

This is the part that usually confuses people, so here is the whole model.

**Process groups.** Every pipeline gets its own process group (pgid = pid of the first
process). The terminal has one *foreground process group*; Ctrl+C sends SIGINT and Ctrl+Z
sends SIGTSTP to **that whole group**. So `sleep 30 | cat` stops/dies as one unit.

**Startup (`init_interactive`).** The shell puts itself in its own group, makes that group the
terminal's foreground group with `tcsetpgrp`, saves the terminal settings (`termios`), and
*ignores* SIGTSTP/SIGTTIN/SIGTTOU/SIGQUIT (so Ctrl+Z can't suspend the shell itself).
SIGINT is caught only to redraw the prompt.

**Starting a foreground job.** Both parent and child call `setpgid(pid, pgid)` — whichever runs
first wins, which removes the race. The job gets the terminal with `tcsetpgrp(job.pgid)`.
Children reset all signals to default before `exec` (ignored signals would otherwise be inherited).

**Waiting.** `waitpid(-1, …, WUNTRACED)` returns when a child *exits or stops*. The job table
tracks every process; a job is *completed* when all processes are, *stopped* when every
live one is.

**After the wait**, the shell takes the terminal back (`tcsetpgrp(shell)`) and restores its saved
`termios` (a program like `vim` may have left the terminal in raw mode). If the job was
stopped, its terminal modes are saved and restored when you `fg` it again.

**Commands**

| Command | What happens |
|---|---|
| `cmd &` | fork, do not wait, print `[1] pid`; the shell keeps the terminal |
| Ctrl+Z | child stops → `waitpid` returns → job marked *Stopped*, prompt returns |
| `bg %n` | send `SIGCONT`, leave it in the background |
| `fg %n` | give it the terminal, restore its modes, `SIGCONT`, wait again |
| `jobs` | print the table, `+` = current job, `-` = previous |
| `kill %n` | signal the whole group; a stopped job also gets `SIGCONT` so TERM can be delivered |

Finished background jobs are reaped with `WNOHANG` before every prompt and announced
(`[1]+ Done  sleep 5`). `exit` with stopped jobs warns once, a second `exit` hangs them up.

## 6. Bugs found while testing (good interview stories)

1. **Shared file offset.** The shell read scripts through a buffered `FILE*`. A forked child that
   called `fflush(NULL)` made libc `lseek` the file *backwards* (to give back unread buffered
   bytes). Because parent and child share one file offset, the parent then **re-read a line
   and ran it twice** (seen under valgrind). Fix: flush only `stdout`/`stderr`, read script
   files fully into memory, and read piped stdin one byte at a time with `read(2)`.
2. **Variables and globbing.** `G='*.txt'; echo $G` must glob (POSIX); an early version treated
   wildcards coming from variables as literal text. Caught by comparing against `dash`.
3. **Reference shells disagree** (dash returns 2 for a failed `cd`, bash 1), so tests check
   "failed", not the exact number.

## 7. Testing strategy

| Test | What it covers |
|---|---|
| `tests/test_lexer.cpp` | quoting, operators, variables, splitting, globbing, errors (50 checks) |
| `tests/test_parser.cpp` | grammar, precedence, redirection fds, continuation, syntax errors (58 checks) |
| `tests/scripts/*.sh` | the same script run through **mysh and dash**, output + exit code must match |
| `tests/pty_jobs.py` | runs mysh in a real pseudo-terminal and types Ctrl+C, Ctrl+Z, `bg`, `fg`, `kill %1`, `exit` |
| valgrind | no leaks / invalid memory use |

## 8. Ideas to extend it (pick one for your resume)

* `$(...)` command substitution and `<<` here-documents
* `if / while / for` and shell functions (needs a bigger grammar and an AST evaluator)
* Tab completion of commands and files (readline completion hook)
* `$PATH` lookup cache (hash table, like bash `hash`) and aliases
* Replace `fork` with `posix_spawn` for speed and compare
