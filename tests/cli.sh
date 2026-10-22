#!/bin/sh
# Command-line behaviour that tests/run.sh cannot express: arguments,
# exit codes, the REPL fed through a pipe, the exact shape of an error.
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

check no-arguments        1 "version required"            "$bin"
check no-arguments-usage  1 "installed: 0.1"              "$bin"
check not-installed       1 "Arete 2.0 is not installed"  "$bin" 2.0
check not-installed-hint  1 "arete install 2.0"           "$bin" 2.0
check unknown-command     1 "unknown command"             "$bin" frobnicate
check run-without-file    1 "missing file"                "$bin" run
check run-missing-file    1 "cannot open file"            "$bin" run tests/does_not_exist.rt
check run-extra-argument  1 "unexpected argument \`extra\`" "$bin" run tests/hello.rt extra
check repl-with-file      1 "to run a file: arete run tests/hello.rt" "$bin" 0.1 tests/hello.rt
check run-hello           0 "Hello, World!"               "$bin" run tests/hello.rt

# The REPL through a pipe: no banner, no prompt, one result per line.
out="$(printf 'print("hi")\nprint("there")\n' | "$bin" 0.1 2>&1)"
code=$?
if [ "$out" != "hi
there" ] || [ "$code" -ne 0 ]; then
    echo "FAIL repl-pipe (exit $code): $out"; fail=1
fi

# An error in the REPL is reported, the session continues, the exit code is 1.
out="$(printf 'print(1 // 0)\nprint("ok")\n' | "$bin" 0.1 2>/dev/null)"
code=$?
if [ "$out" != "ok" ] || [ "$code" -ne 1 ]; then
    echo "FAIL repl-continues-after-error (exit $code): $out"; fail=1
fi
err="$(printf 'print(\n' | "$bin" 0.1 2>&1 >/dev/null)"
case "$err" in
    *"unclosed \`(\`"*) ;;
    *) echo "FAIL repl-error-text: $err"; fail=1 ;;
esac

# A REPL line needs no version header, and typing one gets a specific message.
out="$(printf 'print("no header needed")\n' | "$bin" 0.1 2>&1)"
[ "$out" = "no header needed" ] || { echo "FAIL repl-no-header: $out"; fail=1; }
err="$(printf 'arete 0.1\n' | "$bin" 0.1 2>&1 >/dev/null)"
case "$err" in
    *"version header belongs on line 1 only"*"no header is needed in the REPL"*) ;;
    *) echo "FAIL repl-header-typed: $err"; fail=1 ;;
esac
err="$(printf '  print("x")\n' | "$bin" 0.1 2>&1 >/dev/null)"
case "$err" in
    *"the first line of a REPL input starts at column 1"*) ;;
    *) echo "FAIL repl-indent-hint: $err"; fail=1 ;;
esac

# Multi-line input: a block runs when the blank line closes it (phase 8).
out="$(printf 'if true:\n    print(1)\n\n' | "$bin" 0.1 2>&1)"
[ "$out" = "1" ] || { echo "FAIL repl-block: $out"; fail=1; }
out="$(printf 'if true:\n    if false:\n        print("no")\n    else:\n        print("nested")\nprint("after")\n' | "$bin" 0.1 2>&1)"
[ "$out" = "nested
after" ] || { echo "FAIL repl-nested-block: $out"; fail=1; }
out="$(printf 'let x = (1 +\n    2)\nprint(x)\n' | "$bin" 0.1 2>&1)"
[ "$out" = "3" ] || { echo "FAIL repl-open-bracket: $out"; fail=1; }
out="$(printf 'if true:\n    print(1 // 0)\n\nprint("alive")\n' | "$bin" 0.1 2>/dev/null)"
code=$?
[ "$out" = "alive" ] && [ "$code" -eq 1 ] || { echo "FAIL repl-error-in-block (exit $code): $out"; fail=1; }
err="$(printf 'if true:\nprint(2)\n\n' | "$bin" 0.1 2>&1 >/dev/null)"
case "$err" in
    *"expected an indented block after \`if\`"*) ;;
    *) echo "FAIL repl-block-not-indented: $err"; fail=1 ;;
esac
out="$(printf 'print("a:")\nprint(1)  # comment ending with a colon:\n' | "$bin" 0.1 2>&1)"
[ "$out" = "a:
1" ] || { echo "FAIL repl-colon-in-string-or-comment: $out"; fail=1; }

# The interactive path (banner, prompts, history) through a pseudo-terminal.
case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*) ;;
    *) if command -v python3 >/dev/null 2>&1; then
           python3 -I tests/repl_pty.py "$bin" || fail=1
       fi ;;
esac

# Functions declared in one REPL input are callable in the next, and may be
# redefined; an error inside such a function shows the line of the input
# that declared it.
out="$(printf 'fn f() -> int:\n    return 7\n\nprint(f())\n' | "$bin" 0.1 2>&1)"
[ "$out" = "7" ] || { echo "FAIL repl-fn-across-inputs: $out"; fail=1; }
out="$(printf 'fn f() -> int:\n    return 1\n\nfn f() -> int:\n    return 2\n\nprint(f())\n' | "$bin" 0.1 2>&1)"
[ "$out" = "2" ] || { echo "FAIL repl-fn-redefined: $out"; fail=1; }
err="$(printf 'fn f() -> int:\n    return 1 // 0\n\nprint("before")\nf()\n' | "$bin" 0.1 2>&1 >/dev/null)"
case "$err" in
    *"<repl>:2:14"*" 2 |     return 1 // 0"*) ;;
    *) echo "FAIL repl-error-in-earlier-fn: $err"; fail=1 ;;
esac
out="$(printf 'return 1\nprint("still here")\n' | "$bin" 0.1 2>/dev/null)"
code=$?
if [ "$out" != "still here" ] || [ "$code" -ne 1 ]; then
    echo "FAIL repl-return-at-top-level (exit $code): $out"; fail=1
fi
out="$(printf 'let c = make()\n' | "$bin" 0.1 2>&1)"
case "$out" in
    *"unknown function \`make\`"*) ;;
    *) echo "FAIL repl-unknown-function: $out"; fail=1 ;;
esac

# Empty input and empty lines are fine.
out="$(printf '' | "$bin" 0.1 2>&1)"
[ "$out" = "" ] || { echo "FAIL repl-empty-input: $out"; fail=1; }
out="$(printf '\n\nprint("after blanks")\n' | "$bin" 0.1 2>&1)"
[ "$out" = "after blanks" ] || { echo "FAIL repl-blank-lines: $out"; fail=1; }

# The exact shape of an error block (SPEC.md section 4).
err="$("$bin" run tests/no_version.rt 2>&1 >/dev/null)"
want='error: missing version header
  --> tests/no_version.rt:1:1
   |
 1 | print("Hello, World!")
   | ^ expected `arete 0.1` here
   = help: add `arete 0.1` at the top of the file'
[ "$err" = "$want" ] || { echo "FAIL error-shape:"; echo "$err"; fail=1; }

# The caret lands under the right character after a non-ASCII string and a tab.
err="$("$bin" run tests/caret_after_utf8.rt 2>&1 >/dev/null)"
want='error: unexpected character `$`
  --> tests/caret_after_utf8.rt:2:15
   |
 2 | print("éé")	$
   |            	^ here'
[ "$err" = "$want" ] || { echo "FAIL caret-alignment:"; echo "$err"; fail=1; }

# The REPL keeps names between lines, allows redeclaration, and a failed
# declaration leaves a name without a value.
out="$(printf 'let x = 1\nprint(x)\nlet x = "s"\nprint(x)\nvar n = 2\nn += 1\nprint(n)\n' | "$bin" 0.1 2>&1)"
[ "$out" = "1
s
3" ] || { echo "FAIL repl-names-persist: $out"; fail=1; }
err="$(printf 'let z = 1 // 0\nprint(z)\n' | "$bin" 0.1 2>&1 >/dev/null)"
case "$err" in
    *"division by zero"*"\`z\` has no value"*) ;;
    *) echo "FAIL repl-failed-declaration: $err"; fail=1 ;;
esac
err="$(printf 'if true:\n' | "$bin" 0.1 2>&1 >/dev/null)"
case "$err" in
    *"expected an indented block after \`if\`"*) ;;
    *) echo "FAIL repl-block-at-eof: $err"; fail=1 ;;
esac

# A very long operator chain is refused, not a crash.
long="$(mktemp)"
{ printf 'arete 0.1\nprint(1'; i=0; while [ $i -lt 1200 ]; do printf ' + 1'; i=$((i+1)); done; printf ')\n'; } > "$long"
check long-chain 1 "expression nested too deeply" "$bin" run "$long"
{ printf 'arete 0.1\nprint('; i=0; while [ $i -lt 1200 ]; do printf -- '-'; i=$((i+1)); done; printf '1)\n'; } > "$long"
check long-prefix-chain 1 "expression nested too deeply" "$bin" run "$long"
{ printf 'arete 0.1\nprint(1'; i=0; while [ $i -lt 900 ]; do printf ' + 1'; i=$((i+1)); done; printf ')\n'; } > "$long"
check chain-within-limit 0 "901" "$bin" run "$long"
rm -f "$long"

# Output that cannot be written is an error, not a silent success. Only
# where /dev/full really refuses writes: Linux. macOS has no /dev/full and
# Git Bash on Windows emulates it inconsistently.
case "$(uname -s 2>/dev/null)" in MINGW*|MSYS*|CYGWIN*) have_dev_full=0 ;; *) have_dev_full=1 ;; esac
if [ "$have_dev_full" -eq 1 ] && [ -w /dev/full ] && ! printf x > /dev/full 2>/dev/null; then
    check write-failure 1 "cannot write output" sh -c "$bin run tests/hello.rt > /dev/full"
fi

# Deep nesting is refused at 65 levels with a clear message.
deep="$(mktemp)"
{ printf 'arete 0.1\nprint('; i=0; while [ $i -lt 64 ]; do printf '('; i=$((i+1)); done
  printf '"x"'; i=0; while [ $i -lt 65 ]; do printf ')'; i=$((i+1)); done; printf '\n'; } > "$deep"
check deep-parens 1 "too many nested parentheses" "$bin" run "$deep"
rm -f "$deep"

if [ "$fail" -eq 0 ]; then
    echo "cli: all passed"
else
    exit 1
fi
