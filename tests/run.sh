#!/bin/sh
# Runs every <name>.rt in the given directory and compares stdout with <name>.expected.
# If <name>.expected_error exists instead, the program must fail and stderr must
# contain that text. Exit code 1 if any test fails. Phase 3 fills this in further.
set -u
dir="${1:-tests}"
bin="./arete"
pass=0; fail=0
for src in "$dir"/*.rt; do
    [ -e "$src" ] || continue
    base="${src%.rt}"
    name="$(basename "$base")"
    if [ -f "$base.expected" ]; then
        actual="$("$bin" run "$src" 2>/dev/null)"
        expected="$(cat "$base.expected")"
        if [ "$actual" = "$expected" ]; then
            pass=$((pass+1))
        else
            fail=$((fail+1)); echo "FAIL $name"
            echo "  expected: $expected"; echo "  actual:   $actual"
        fi
    elif [ -f "$base.expected_error" ]; then
        err="$("$bin" run "$src" 2>&1 >/dev/null)"; code=$?
        want="$(cat "$base.expected_error")"
        if [ "$code" -ne 0 ] && printf '%s' "$err" | grep -qF -- "$want"; then
            pass=$((pass+1))
        else
            fail=$((fail+1)); echo "FAIL $name (expected error containing: $want)"
            echo "  got (exit $code): $err"
        fi
    else
        echo "SKIP $name (no .expected or .expected_error)"
    fi
done
echo "passed: $pass  failed: $fail"
[ "$fail" -eq 0 ]
