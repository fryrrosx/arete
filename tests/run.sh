#!/bin/sh
# Runs every <name>.rt directly inside the given directory and compares:
#   <name>.expected        exact stdout, and the exit code must be 0
#   <name>.expected_error  the program must fail (exit code 1) and stderr
#                          must contain this text (the error's summary line)
# Subdirectories are not entered. Exit code 1 if any test fails.
set -u
dir="${1:-tests}"
bin="./arete"
pass=0
fail=0
for src in "$dir"/*.rt; do
    [ -e "$src" ] || continue
    base="${src%.rt}"
    name="$(basename "$base")"
    if [ -f "$base.expected" ]; then
        actual="$("$bin" run "$src" 2>/dev/null)"
        code=$?
        expected="$(cat "$base.expected")"
        if [ "$code" -eq 0 ] && [ "$actual" = "$expected" ]; then
            pass=$((pass + 1))
        else
            fail=$((fail + 1))
            echo "FAIL $name (exit $code)"
            echo "  expected: $expected"
            echo "  actual:   $actual"
        fi
    elif [ -f "$base.expected_error" ]; then
        err="$("$bin" run "$src" 2>&1 >/dev/null)"
        code=$?
        want="$(cat "$base.expected_error")"
        if [ "$code" -eq 1 ] && printf '%s' "$err" | grep -qF -- "$want"; then
            pass=$((pass + 1))
        else
            fail=$((fail + 1))
            echo "FAIL $name (expected exit 1 and error containing: $want)"
            echo "  got (exit $code): $err"
        fi
    else
        echo "SKIP $name (no .expected or .expected_error)"
    fi
done
echo "$dir: passed $pass, failed $fail"
[ "$fail" -eq 0 ]
