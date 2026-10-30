#!/usr/bin/env python3
"""Interactive job-control test: runs mysh inside a real pseudo-terminal and
types Ctrl+C / Ctrl+Z / fg / bg at it. Usage: tests/pty_jobs.py ./mysh"""
import os, pty, re, select, sys, time

MYSH = sys.argv[1] if len(sys.argv) > 1 else "./mysh"
ANSI = re.compile(rb"\x1b\[[0-9;?]*[A-Za-z]|\x01|\x02|\r")

pid, fd = pty.fork()
if pid == 0:
    os.environ["HOME"] = "/tmp/mysh_pty_home"
    os.makedirs("/tmp/mysh_pty_home", exist_ok=True)
    os.environ["TERM"] = "dumb"
    os.execv(MYSH, [MYSH])

buf = b""
def pump(t=0.4):
    global buf
    end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try:
                d = os.read(fd, 4096)
            except OSError:
                return
            if not d:
                return
            buf += d
def send(s, wait=0.5):
    os.write(fd, s.encode() if isinstance(s, str) else s)
    pump(wait)
def text():
    return ANSI.sub(b"", buf).decode(errors="replace")

fails = []
def check(name, cond):
    print(("ok    " if cond else "FAIL  ") + name)
    if not cond: fails.append(name)

pump(0.8)
check("prompt appears", "mysh:" in text())

# --- Ctrl+C kills the foreground job, shell survives ----------------------
mark = len(buf)
send("sleep 30\n", 0.5)
send("\x03", 0.6)                       # Ctrl+C
send("echo AFTER-CTRL-C\n", 0.5)
t = ANSI.sub(b"", buf[mark:]).decode(errors="replace")
check("Ctrl+C kills sleep, shell survives", "AFTER-CTRL-C" in t)

# --- Ctrl+C at an empty prompt does not exit the shell --------------------
send("\x03", 0.4)
send("echo STILL-HERE\n", 0.5)
check("Ctrl+C at prompt keeps shell alive", "STILL-HERE" in text())

# --- Ctrl+Z stops, jobs lists it, bg resumes, fg brings back --------------
mark = len(buf)
send("sleep 30\n", 0.5)
send("\x1a", 0.7)                       # Ctrl+Z
t = ANSI.sub(b"", buf[mark:]).decode(errors="replace")
check("Ctrl+Z reports [1]+ Stopped", re.search(r"\[1\]\+\s+Stopped\s+sleep 30", t) is not None)

mark = len(buf)
send("jobs\n", 0.5)
t = ANSI.sub(b"", buf[mark:]).decode(errors="replace")
check("jobs lists the stopped job", "Stopped" in t and "sleep 30" in t)

mark = len(buf)
send("bg\n", 0.5)
send("jobs\n", 0.5)
t = ANSI.sub(b"", buf[mark:]).decode(errors="replace")
check("bg resumes it (Running ... &)", re.search(r"Running\s+sleep 30 &", t) is not None)

mark = len(buf)
send("fg\n", 0.5)
send("\x03", 0.6)                       # kill it with Ctrl+C
send("echo BACK-FROM-FG\n", 0.5)
t = ANSI.sub(b"", buf[mark:]).decode(errors="replace")
check("fg foregrounds it, Ctrl+C ends it", "sleep 30" in t and "BACK-FROM-FG" in t)

mark = len(buf)
send("jobs\n", 0.4)
t = ANSI.sub(b"", buf[mark:]).decode(errors="replace")
check("job table empty afterwards", "sleep" not in t.split("jobs", 1)[-1])

# --- background job: starts, finishes, gets reported ----------------------
mark = len(buf)
send("sleep 0.3 &\n", 0.3)
t = ANSI.sub(b"", buf[mark:]).decode(errors="replace")
check("& prints [n] pid", re.search(r"\[\d+\] \d+", t) is not None)
time.sleep(0.6)
send("echo tick\n", 0.6)
t = ANSI.sub(b"", buf[mark:]).decode(errors="replace")
check("finished bg job reported as Done", re.search(r"\[\d+\]\+?\s+Done\s+sleep 0.3", t) is not None)

# --- whole pipeline is one job and stops together --------------------------
mark = len(buf)
send("sleep 30 | cat\n", 0.5)
send("\x1a", 0.7)
t = ANSI.sub(b"", buf[mark:]).decode(errors="replace")
check("pipeline stops as one job", re.search(r"Stopped\s+sleep 30 \| cat", t) is not None)
send("kill %1\n", 0.6)
send("echo KILLED-STOPPED-PIPE\n", 0.6)
check("kill %1 terminates a stopped pipeline", "KILLED-STOPPED-PIPE" in text())

# --- foreground job can read the terminal ----------------------------------
mark = len(buf)
send("cat\n", 0.4)
send("hello-from-tty\n", 0.5)
send("\x04", 0.5)                       # Ctrl+D = EOF to cat
t = ANSI.sub(b"", buf[mark:]).decode(errors="replace")
check("foreground job reads the tty", t.count("hello-from-tty") >= 2)

# --- exit warns if jobs are stopped ----------------------------------------
send("sleep 30\n", 0.4)
send("\x1a", 0.6)
mark = len(buf)
send("exit\n", 0.6)
t = ANSI.sub(b"", buf[mark:]).decode(errors="replace")
check("exit warns about stopped jobs", "stopped jobs" in t)
send("exit\n", 0.8)
try:
    wpid, st = os.waitpid(pid, os.WNOHANG)
    check("second exit really exits", wpid == pid)
except ChildProcessError:
    check("second exit really exits", True)

print("\nFAILED: %s" % fails if fails else "\nall pty checks passed")
sys.exit(1 if fails else 0)
