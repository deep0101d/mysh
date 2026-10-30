#!/bin/sh
# Runs every tests/scripts/*.sh through mysh AND dash, and compares stdout+exit code.
# Usage: tests/run_scripts.sh ./mysh
MYSH=${1:-./mysh}
DIR=$(dirname "$0")/scripts
REF=${REF_SHELL:-dash}
pass=0; fail=0
for f in "$DIR"/*.sh; do
    name=$(basename "$f")
    exp=$($REF "$f" 2>/dev/null); exp_rc=$?
    got=$($MYSH "$f" 2>/dev/null); got_rc=$?
    if [ "$exp" = "$got" ] && [ "$exp_rc" = "$got_rc" ]; then
        printf 'ok    %s\n' "$name"; pass=$((pass+1))
    else
        printf 'FAIL  %s (exit: expected %s, got %s)\n' "$name" "$exp_rc" "$got_rc"
        printf '%s\n' "$exp" > /tmp/mysh_exp.$$; printf '%s\n' "$got" > /tmp/mysh_got.$$
        diff /tmp/mysh_exp.$$ /tmp/mysh_got.$$ | head -15
        rm -f /tmp/mysh_exp.$$ /tmp/mysh_got.$$
        fail=$((fail+1))
    fi
done
echo "scripts: $pass passed, $fail failed"
[ "$fail" = 0 ]
