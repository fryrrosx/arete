# Review of karim_01.rt

Your first sketch, line by line, against SPEC.md as it stands today.
Three things are already correct Arete and worth noticing first:

- `let aa: int = 12` is exactly right: `let`, a name, an optional type, `=`, a value.
- The whole `if aa == 12: / elif aa > 12: / else:` block is correct, including
  the 4-space indentation.
- The `when aa:` block with `12 ->` and `else ->` is correct.

Everything below is what the compiler would say, in file order.

| Your line | What happens | How to write it |
|---|---|---|
| `use 'arete'->(version;0.1);` | Line 1 must be exactly the version header. `use` is for packages and your own modules, `;` does not exist, `->` is the arrow of `when` branches and return types. | `arete 0.1` |
| `from linux usage use kali`, `use terminal` | Not code. The idea behind it is good: running shell commands from a program. In Arete that is the `process` module, which needs the `exec` right. It arrives in phase 21. | `use "process" allow [exec]` (spelling of the standard library is decided in phase 19) |
| `run arete 0.1` | That is a terminal command, not program text. | type `arete run file.rt` in the shell |
| `let pt1: = float(int(input))` | A `:` must be followed by a type, or left out. Reading a line from the user is `input()` in Python; the Arete name is decided in phase 21. `int(...)` and `float(...)` conversions are decided in phase 9. | `let pt1 = float(input())` or `let pt1: float = float(input())` |
| ` pt1 == aa` | A comparison on its own line computes `true` or `false` and throws it away. The leading space is `unexpected indentation`. `==` compares, `=` assigns. | `let same = pt1 == aa` or `if pt1 == aa:` |
| `aa == terminal and bash usage from linux` | A note, so it needs `#`. | `# aa stands for ...` |
| `elif: pt1 >= 46:` | The condition goes between `elif` and `:`. Also `46 >= 14`, so this branch could never run after the first one; test the bigger number first. | `elif pt1 >= 46:` placed before `elif pt1 >= 14:` |
| `print("We are sorry ... low secured)` | The closing `"` is missing: `unterminated string literal`, reported at the opening quote. | `print("... low secured")` |
| `if result >= 14 or =>46:` | `result` was never declared. Each side of `or` is a complete comparison. `=>` is reserved for lambdas; "greater or equal" is `>=`. | `if result >= 14 or result >= 46:` |
| `result == secured` | Compares, does not assign, and `secured` is an undeclared name. If it is text, it is a string. | `var result = "secured"` |
| `if termainl == secured:` | Typo in `terminal`; `secured` again needs quotes or a declaration. | `if terminal == "secured":` |
| `def:` | `def` is Python's word for a function. In Arete a function is `fn name():` and always has a name. A block cannot be opened by a bare keyword. | `fn check():` |
| `output == successful:` | A comparison cannot open a block; only `if`, `elif`, `else`, `while`, `for`, `fn`, `class`, `when`, `try`, `catch` can. | `if output == "successful":` |

The one rule that would have caught most of this: every line is either a
declaration (`let`, `var`, `fn`, `class`), a statement that does something
(`print(...)`, `x = ...`, `return`), or a block opener that ends in `:`.
A bare expression such as `pt1 == aa` is legal but useless, and the linter in
phase 22 will warn about it.

Two ideas from your sketch are now in SPEC.md: `input()` as the candidate
name for reading a line (section 2.7, `io`), and shell access through the
`process` module with the `exec` right (already there).

The same program in today's Arete:

```
arete 0.1

let aa: int = 12
let pt1 = float(input())        # names of input() and float() are decided in phases 21 and 9

if aa == 12:
    print("text")
elif aa > 12:
    print("big")
else:
    print("small")

if pt1 >= 46:
    print("well secured!")
elif pt1 >= 14:
    print("secured")
else:
    print("We are sorry to admit, but the number is low secured")

when aa:
    12 -> print("twelve")
    else -> print("other")

var result = "secured" if pt1 >= 14 else "low"
if result == "secured":
    print("you got the control")
```
