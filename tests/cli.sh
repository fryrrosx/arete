#!/bin/sh
# Command-line behaviour that tests/run.sh cannot express: arguments,
# exit codes, the REPL fed through a pipe.
set -u
bin="./arete"
fail=0

# check <name> <expected exit code> <text that must appear> <command...>
check() {
    name="$1"; want_code="$2"; needle="$3"; shift 3
    out="$("$@" 2>&1)"
    code=$?
    if [ "$code" -ne "$want_code" ] || ! printf '%s' "$out" | grep -qF -- "$needle"; then
        echo "FAIL $name (exit $code, wanted $want_code containing: $needle)"
        echo "  got: $out"
        fail=1
    fi
}

check no-arguments      1 "version required"            "$bin"
check no-arguments-usage 1 "installed: 0.1"             "$bin"
check not-installed     1 "Arete 2.0 is not installed"  "$bin" 2.0
check not-installed-hint 1 "arete install 2.0"          "$bin" 2.0
check unknown-command   1 "unknown command"             "$bin" frobnicate
check run-without-file  1 "usage: arete run"            "$bin" run
check run-missing-file  1 "cannot open file"            "$bin" run tests/does_not_exist.rt
check run-hello         0 "Hello, World!"               "$bin" run tests/hello.rt

# The REPL through a pipe: no banner, no prompt, one result per line.
out="$(printf 'print("hi")\nprint("there")\n' | "$bin" 0.1 2>&1)"
if [ "$out" != "hi
there" ]; then
    echo "FAIL repl-pipe: $out"; fail=1
fi

# An error in the REPL is reported and the session continues.
out="$(printf 'print(\nprint("ok")\n' | "$bin" 0.1 2>/dev/null)"
if [ "$out" != "ok" ]; then
    echo "FAIL repl-continues-after-error: $out"; fail=1
fi
err="$(printf 'print(\n' | "$bin" 0.1 2>&1 >/dev/null)"
case "$err" in
    *"unclosed \`(\`"*) ;;
    *) echo "FAIL repl-error-text: $err"; fail=1 ;;
esac

# A REPL line needs no version header, and typing one is just an error.
out="$(printf 'print("no header needed")\n' | "$bin" 0.1 2>&1)"
[ "$out" = "no header needed" ] || { echo "FAIL repl-no-header: $out"; fail=1; }
err="$(printf 'arete 0.1\n' | "$bin" 0.1 2>&1 >/dev/null)"
case "$err" in
    "error: "*) ;;
    *) echo "FAIL repl-header-typed: $err"; fail=1 ;;
esac

# Empty input and an empty line are fine.
out="$(printf '' | "$bin" 0.1 2>&1)"
[ "$out" = "" ] || { echo "FAIL repl-empty-input: $out"; fail=1; }
out="$(printf '\n\nprint("after blanks")\n' | "$bin" 0.1 2>&1)"
[ "$out" = "after blanks" ] || { echo "FAIL repl-blank-lines: $out"; fail=1; }

if [ "$fail" -eq 0 ]; then
    echo "cli: all passed"
else
    exit 1
fi
