# mysh — a small Unix shell in C++17

A from-scratch shell with pipes, redirection, `&&` / `||` / `;`, quoting,
`$VARIABLES`, globbing, background jobs and real job control (Ctrl+C, Ctrl+Z,
`fg`, `bg`, `jobs`). About 1,500 lines, no dependencies except (optionally) GNU readline.

## Build & run

```bash
sudo apt install build-essential libreadline-dev   # readline is optional
make            # builds ./mysh   (auto-detects readline)
./mysh          # interactive
./mysh script.sh
./mysh -c 'echo hi | tr a-z A-Z'
echo 'ls | wc -l' | ./mysh
make test       # unit tests + script tests (vs dash) + pty job-control test
```

Works on Linux and macOS. On Windows use WSL.

## Features

| Feature | Example |
|---|---|
| Pipes | `cat f \| grep x \| sort \| uniq -c` |
| Redirection | `< in`, `> out`, `>> out`, `2> err`, `2>&1`, `> f 2>&1` |
| Lists | `a; b`, `a && b`, `a \|\| b`, `a && b \|\| c` |
| Background | `sleep 10 &`, `make && ./run &` |
| Job control | Ctrl+Z stop, Ctrl+C interrupt, `jobs`, `fg %1`, `bg`, `kill %2`, `wait` |
| Quoting | `'single'`, `"double $VAR"`, `a\ b`, line continuation `\` |
| Variables | `X=1`, `$X`, `${X}`, `$?`, `$$`, `$!`, `FOO=bar cmd`, `export`, `unset` |
| Globbing | `*.c`, `?.txt`, `[ab]*` |
| Misc | `~` expansion, `#` comments, multi-line input, history (↑/↓ with readline), `source` |
| Builtins | `cd pwd echo export unset exit history source . help jobs fg bg kill wait` |

**Not supported (by design, to keep it small):** `$(...)` / backticks, subshells `( )`,
here-documents `<<`, `if/for/while`, functions, aliases. These print a clear error.

## Layout

```
include/   lexer.h parser.h executor.h builtins.h jobs.h signals.h shell.h utils.h
src/       one .cpp per header + main.cpp
tests/     test_lexer.cpp  test_parser.cpp  scripts/*.sh  run_scripts.sh  pty_jobs.py
docs/      design.md   <- how it works; read this before an interview
```

See [docs/design.md](docs/design.md) for the architecture and the reasoning behind each piece.
