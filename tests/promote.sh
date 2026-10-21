#!/bin/sh
# Promotes an example whose phase is reached:
#   sh tests/promote.sh examples/pending/01_hello.rt
# moves it to examples/ and writes examples/<name>.expected from the
# "# expected output:" comment block, or examples/<name>.expected_error from
# the first "# error: ..." line after "# expected compile error:".
set -eu
src="$1"
name="$(basename "$src" .rt)"
dest="examples/$name.rt"
if grep -q '^# expected output:' "$src"; then
    awk '
        found && /^#/ { sub(/^# ?/, ""); print; next }
        found         { exit }
        /^# expected output:/ { found = 1 }
    ' "$src" > "examples/$name.expected"
elif grep -q '^# expected compile error:' "$src"; then
    awk '
        /^# expected compile error:/ { found = 1; next }
        found && /^# error: / { sub(/^# error: /, ""); print; exit }
    ' "$src" > "examples/$name.expected_error"
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
