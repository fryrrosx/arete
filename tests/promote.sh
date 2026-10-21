#!/bin/sh
# Promotes an example whose phase is reached:
#   sh tests/promote.sh examples/pending/01_hello.rt
# It runs the example first; only a passing example moves to examples/, with
# examples/<name>.expected written from the "# expected output:" block, or
# examples/<name>.expected_error from the first "# error: ..." line after
# "# expected compile error:". The block must be the last comment in the file.
set -eu
cd "$(dirname "$0")/.."
src="$1"
[ -f "$src" ] || { echo "promote: no such file: $src" >&2; exit 1; }
name="$(basename "$src" .rt)"
dest="examples/$name.rt"
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT

if grep -q '^# expected output:' "$src"; then
    awk '
        found && /^#/ { sub(/^# ?/, ""); print; next }
        found         { exit }
        /^# expected output:/ { found = 1 }
    ' "$src" > "$tmp"
    if ! ./arete run "$src" | cmp -s - "$tmp"; then
        echo "promote: $src does not produce its expected output yet" >&2
        ./arete run "$src" | diff "$tmp" - >&2 || true
        exit 1
    fi
    cp "$tmp" "examples/$name.expected"
elif grep -q '^# expected compile error:' "$src"; then
    awk '
        /^# expected compile error:/ { found = 1; next }
        found && /^# error: / { sub(/^# error: /, ""); print; exit }
    ' "$src" > "$tmp"
    if ./arete run "$src" >/dev/null 2>&1 || ! ./arete run "$src" 2>&1 | grep -qF -- "$(cat "$tmp")"; then
        echo "promote: $src does not fail with the expected error yet" >&2
        exit 1
    fi
    cp "$tmp" "examples/$name.expected_error"
else
    echo "promote: no '# expected output:' or '# expected compile error:' block in $src" >&2
    exit 1
fi
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git mv "$src" "$dest"
else
    mv "$src" "$dest"
fi
echo "promoted $dest"
