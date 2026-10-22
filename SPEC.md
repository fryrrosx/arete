# Arete Language Specification

Version: draft v1 (phase 1, 2026-10-20). Language version in file headers: `arete 0.1`.

This document is the single source of truth for language decisions. Code never
contradicts it; if an implementation step shows a decision is wrong, the step
stops and the decision is revisited here, with a note of the phase that changed it.

**Status legend used in the tables**

| Status | Meaning |
|---|---|
| PDF | Decided in the design discussion summarized on 2026-10-19 |
| P1 | Decided by Karim in phase 1 (2026-10-20) |
| P1-default | Proposed by Claude in phase 1 to keep examples consistent. Stands until Karim objects |
| OPEN(n) | Not decided yet. Decided in phase n (see `ROADMAP.md`) |
| PDF-implied | Follows from an example in the design discussion, not a stated decision |
| Pn | Decided by Karim in phase n; P1 is the first instance |

**Terms used below**

- *REPL*: read-eval-print loop, the interactive console started by `arete 0.1`.
- *semver*: version numbers `major.minor.patch`; before 1.0 any minor may break compatibility.
- *lock file*: `arete.lock`, generated, records the exact version of every package a program uses.
- *smart cast*: after `if x != null:` the compiler treats `x` as non-null inside the block.
- *structural equality*: two values are equal when all their fields are equal, not when they are the same object.
- *short-circuit*: `a and b` does not evaluate `b` when `a` is false; `a or b` does not evaluate `b` when `a` is true.
- *GC*: garbage collector, a runtime that periodically pauses the program to free memory. Arete uses reference counting instead.
- *logical line*: one statement's worth of text; defined precisely in section 3.

---

## 1. What Arete is

A new programming language built from scratch. Syntax as simple as Python,
static types like TypeScript and Kotlin, performance target of C/C++ (via LLVM,
later). Files end in `.rt`.

Slogan: *All the libraries in the world. Safely. Forever.*

- **All the libraries in the world:** import code from C, Python and JavaScript directly.
- **Safely:** each package receives only the rights it is granted (network, files, environment, processes).
- **Forever:** the language version and dependency versions are pinned in the source file; a program runs identically years later.

A language that "replaces all languages" does not work, because languages make
opposite choices. Arete takes the best of each and resolves conflicts explicitly.

---

## 2. Decisions

### 2.1 Project

| Area | Decision | Status |
|---|---|---|
| Name | Arete. Known collision: Jeremy Siek's experimental language of the same name (github.com/jsiek/arete, Python prototype, dormant since October 2023, 12 stars, a 2023 paper). Also taken: `arete` on crates.io and PyPI by unrelated projects. Keep, rename, or publish as "AreteLang": Karim decides before the first public announcement | PDF / OPEN(24) (collision) |
| File extension | `.rt` | PDF |
| License | MIT | P1 |
| Repository | `github.com/fryrrosx/arete` | P1 |
| Implementation language | C11, GNU make, no dependencies beyond libc. Compiles with gcc, clang, MSVC. A vendored single-file C library is allowed when decided per case; the first is `linenoise` (REPL line editor, POSIX only, BSD-2, `LICENSE-linenoise`) | PDF (C) / P1-default (portability) / P8 (vendoring rule, linenoise) |
| Audience | Developers who share loudly: vibe-coders and Python developers who need speed | PDF |
| Release | GitHub, MIT, builds for Linux/macOS/Windows via GitHub Actions, one-command install, docs site, VS Code extension, semver (0.1 then 1.0), a flagship project written in Arete, an online playground, announcement on Hacker News, r/ProgrammingLanguages, Discord, Twitter/X, YouTube, Reddit | PDF / OPEN(24) (what the flagship project is) |
| Execution | Tree-walking interpreter and REPL for development (phase 3), bytecode VM (phase 27), native code via LLVM for release with `arete build` (phase 32). All three must give identical output on `tests/` | PDF / OPEN(26) (VM architecture) |
| Command line | `arete run <file.rt>` runs a file. `arete <version>` starts the REPL: banner `Arete 0.1 REPL`, prompt `>>> `, continuation prompt `... ` while the input is incomplete (an open bracket, a line ending with `:`, or the lines of a block until a blank line closes it, as in Python). A blank line always ends the input unless a bracket is open, so `if true:` followed by an empty line reports its missing body and returns to `>>> `; once a block has opened, its lines are collected until the blank line and any error inside is reported once, when the block runs. Ctrl-C drops the input typed so far; Ctrl-D at `>>> ` ends the session, Ctrl-D inside an input drops that input and ends the session. When stdin or stdout is not a terminal there is no banner and no prompt, lines are read plainly (so `arete 0.1 > log` still works). On a terminal the line editor offers arrow keys and a history of the session's lines (Linux, macOS: vendored `linenoise`; Windows: the console's own editor). The history is kept in `~/.arete_history`, up to 1000 lines, saved after every line. All errors go to stderr; the exit code is 1 on any error, 0 otherwise; the REPL keeps running after an error and exits 1 at the end if any line failed. Extra command-line arguments are an error. `--dump-tokens` and `--dump-ast` arrive in phase 4 | PDF (run, REPL) / P1-default (stderr, exit code, no-terminal rule) / P3-default (REPL exit code, extra arguments) |

### 2.2 Source files and versions

| Area | Decision | Status |
|---|---|---|
| Version header | Line 1 of every file is exactly `arete <major>.<minor>`. Nothing may precede it: no blank line, no comment, no shebang. Missing header is a compile error | PDF (header) / P1 (strict line 1) |
| Header value now | `arete 0.1` during development. The header is the pinned language version. A 0.x toolchain runs only files with exactly the same `0.x` (minor versions break compatibility before 1.0). At the 1.0 release all files are updated once; from 1.0 on, a file `arete 1.a` runs on any toolchain `1.b` with b ≥ a. Every file of a program, imported modules included, is checked by the same rule, and all files of one program must declare the same version. The version is two decimal integers, never a float: `0.10` is not `0.1` | P1 |
| Line endings and BOM | `\n` and `\r\n` are both line ends. A UTF-8 byte-order mark at the start of a file is skipped. Spaces after the header are allowed; anything else on line 1 is `malformed version header` | P1-default |
| Missing vs malformed | A first line that begins with the word `arete` in any case, followed by a space, a tab or the line end, but is not exactly `arete <major>.<minor>`, is `malformed version header` (this includes leading zeros such as `0.01`, which is a different spelling and therefore a different version). Any other first line is `missing version header`. The same header on any later line, or typed into the REPL, is `version header belongs on line 1 only` | P3-default |
| REPL | Starts only with a version: `arete 0.1`. `arete` alone: `error: version required`, lists installed versions. `arete 2.0` when absent: `error: Arete 2.0 is not installed`, hint `arete install 2.0` | PDF |
| Entry point | Top-level statements run top to bottom, like a Python script. No `main` | P1 |
| Comments | `#` to end of line. No block comments | PDF-implied / P1-default (no block comments) |
| Example files | Line 2 of every file in `examples/pending/` is `# runs-after: phase N`: the first phase after which it must run. Expected output is in a `# expected output:` comment at the end, or a `# expected compile error:` comment for programs that must fail. When its phase is reached and it passes, `tests/promote.sh` (written in phase 3) moves the file to `examples/` and writes the sibling `.expected` or `.expected_error` file from that comment; `make examples` runs everything in `examples/` | P1 / P1-default (promotion mechanism) |
| Encoding | Source is UTF-8. Strings may contain any Unicode. Identifiers are ASCII only for now | P1 |

### 2.3 Lexical rules

| Area | Decision | Status |
|---|---|---|
| Blocks | Indentation, no `;`. A line ending in `:` opens a block | PDF |
| Indentation | Exactly 4 spaces per level. A tab character in indentation is an error. Inside `( [ {` newlines and indentation are ignored | P1 (4 spaces, no tabs) / P1-default (brackets) |
| Identifiers | `[A-Za-z_][A-Za-z0-9_]*`. Convention: `snake_case` for variables and functions, `PascalCase` for types, `UPPER_CASE` for constants (not enforced) | P1 (ASCII) / P1-default (convention) |
| `_` | A lone underscore is the discard name: `let _ = f()`, later `for _ in 1..=3:`. It is never declared, may be used any number of times in one scope, and cannot be read: `print(_)` is the error `` `_` cannot be read `` | P1-default |
| Logical operators | The words `and`, `or`, `not`. Short-circuit. Operands must be `bool`; `1 and true` is an error, there is no truthiness | P1 / P6-default (strict bool) |
| Comparison | `== != < <= > >=`. `==` compares numbers by value across `int` and `float` (`1 == 1.0` is true), strings by content, and values of different kinds are simply unequal. Ordering works between two numbers or two strings; anything else is a runtime error (a compile error from phase 10). No chaining: `a < b < c` is an error | PDF-implied / P6-default (mixed numeric equality, ordering rules) |
| Arithmetic | `+ - * / // % **`. `/` on two ints gives a `float`; `//` is floor division; `%` is remainder with the sign of the divisor (Python semantics); `**` is power, right-associative. Unary minus is an operator, not part of the number: `-7 // 2` is `(-7) // 2`, which is `-4`; `-2 ** 2` is `-(2 ** 2)`, which is `-4` (Python). Full precedence table in section 3. `+` on two strings concatenates them; mixing a string and a number is an error. Division by zero (`/`, `//`, and `0.0 ** -1`) is a runtime error. `int ** int` is an `int`; a negative exponent on an int base is the runtime error `negative exponent for an int base` (write `2.0 ** -1`), so every expression has one static type. `%` is not implemented yet (Karim's exercise after phase 4) | P1 (`/`, `//`) / P1-default (`%` sign, `**`, unary minus) / P4-default (string `+`, division by zero) / P10 (`int ** int` is `int`) |
| Compound assignment | `+= -= *= /= //= %=`. Assignment is a statement, never an expression | P1-default |
| Bitwise operators | None in 0.1: `& ^ ~ << >>` are not operators, and `\|` is only the union-type separator. Revisited when a use case appears | P1-default |
| Integer literals | Decimal, `_` allowed between digits: `1_000_000`. Hex `0xFF`, binary `0b101` | P1-default |
| Float literals | `1.5`, `1e9`, `2.5e-3`. Neither `1.` nor `.5` is allowed, so `1..10` lexes as `1 .. 10` | P1-default |
| String literals | `"..."` and `'...'` are identical. Escapes: `\n \t \\ \" \' \u{1F600}`. Interpolation `{expr}` is always on; literal braces are written `{{` and `}}`, the only spelling. Until phase 11 a `{` or `}` inside a string is the error `string interpolation comes in phase 11`. An unterminated string is an error reported at its opening quote | P1 / P1-default (phase-3 behaviour) |
| Booleans, null | `true`, `false`, `null` | P1-default |
| Reserved words | `and as break catch class continue data defer elif else enum false finally fn for if import in interface is let macro not null open or override pub rethrows return select self sealed spawn super this throw throws true try unsafe use var weak when while` Of these, the modifiers `data open sealed weak override pub` and `allow` are contextual, as in Kotlin: they are keywords only in their position (before `class` or `fn`, after `use`), so `let data = ...` stays legal. All other words are hard-reserved from day one, including ones for later phases | P1-default (contextual modifiers; Karim may switch to hard-reserving everything) |
| Reserved punctuation | `-> => .. ..= ..< ? ?. ?? @ !`. `!=` is always the comparison operator (longest match); only a `!` not followed by `=` is reserved, for a possible macro-call marker decided in phase 26 | P1-default |

### 2.4 Types and values

| Area | Decision | Status |
|---|---|---|
| Typing | Static, inferred: `let x = 5` has type `int`. Annotations optional: `let x: int = 5`. From phase 10 the checker types every expression before the program runs: a variable keeps the type of its first value (`var x = 5` then `x = "a"` is `expected int, found string`), arguments, returns and defaults must match the signature, conditions must be `bool`, `==` between types that can never be equal is an error (`1 == "a"`; numbers compare with each other). An `int` may be used where a `float` is expected (assignment, argument, return, default, a mixed conditional) and is converted on arrival; nothing else converts implicitly. A lambda's parameter types come from its annotations or from the function type the context expects; without either, `parameter `x` needs a type`. A function that returns a value may be passed where `fn(...)` without a result is expected (the result is dropped); `print` fits any one-parameter `fn(T)`. Types of later phases in an annotation (`int?`, `List<int>`, unions) report the phase they arrive in. Calls through a function value are typed by its `fn(...)` type, which carries no parameter names, so a named argument there is checked when the call runs. In the REPL a global may be redeclared with another type; a function checked against the old type keeps running and the runtime net reports `expected float, found string` when the new value reaches it | PDF / P10 (int accepted for float, `==` of unrelated types is an error) / P10-default (the rest) |
| Base types | `int` (64-bit signed), `float` (64-bit), `string` (immutable, UTF-8), `bool`, `null` | PDF names / P1-default sizes |
| Integer overflow | Runtime error. Wrapping operations come later as library functions. A `bigint` type may come in the standard library | P1 |
| Union types | `int \| string`, as in TypeScript | PDF |
| Nullability | Forbidden by default. `string?` is the nullable string type and the only spelling: `string \| null` is an error with hint `write string?`. `??` gives a default, `?.` is a safe call. Checks are static (compile time) | PDF (`?`, `??`) / P1-default (`?.`, single spelling) |
| Strings | Immutable. `s[i]` is a one-character string (there is no `char` type); `s[a..b]` is a slice with the end excluded. Everything else is a method: `s.len()`, `s.upper()`, `s.lower()`, `s.split(",")`, `s.trim()`, `s.starts_with("a")`, `s.find("x")` (names fixed in phase 11); there are no free functions like `len(s)`. `"ab" * 3` repeats, `"ar" in s` tests for a substring, `str(x)` turns any value into its printed form (a built-in function like `print`). Raw strings `r"..."` have no escapes and no interpolation | P1-default (indexing, slicing, no `char`) / P9 (methods only, `*`, `in`) / P9-default (`str()`, raw strings) |
| Collections | Two types per container, as in Kotlin: `List<T>` is read-only, `MutableList<T>` adds `add`, `remove`, `sort`, `xs[i] = v`; likewise `Map<K, V>` / `MutableMap<K, V>` (`m[k] = v`, `remove`) and `Set<T>` / `MutableSet<T>` (`add`, `remove`). A literal `[1, 2]` or `{"a": 1}` takes the type its context expects (`var xs: MutableList<int> = [1, 2]`) and is a `List`/`Map` otherwise. Whether a collection can change is therefore visible in its type, not in `let` vs `var`; phase 30 builds "only immutable data through a channel" on this. `Map` iterates in insertion order, `m[k]` reads, `k in m` tests. Empty `{}` is an empty `Map`; a set is always spelled with its name: `var s: Set<string> = Set()`, `Set([1, 2])`, `MutableSet<int>()`. Common methods: `len()`, `contains` via `in`, `keys()`, `values()`, `sorted()` returns a new list, `sort()` sorts a `MutableList` in place; the full list is fixed in phase 12 | P1-default (literals, insertion order) / P9 (read-only and mutable types, empty `{}`, `Set()` spelling) / P9-default (method names) |
| Variables | `let` immutable (default), `var` mutable. Assigning to a `let` is a compile error; so is using a name before its declaration or declaring the same name twice in one scope. A block (the body of `if`, later loops and functions) is a scope: names declared inside end with it, and an inner block may shadow an outer name. In the REPL a name may be declared again | PDF / P5-default (declare once per scope, use after declaration) / P6-default (block scope, shadowing in inner blocks) |
| Generics | `List<T>`, `fn first<T>(xs: List<T>) -> T`, `class Box<T>`, constraints `T: Comparable`. Explicit type arguments at a call are written `json<User>()`: after a name, `<` starts type arguments when a type, `>` and `(` follow (TypeScript's rule); a comparison of the shape `a < b > (c)` is refused, which chained comparisons already are. Usually the type is inferred (`let u: User = res.json()`). Type arguments exist at run time (reified): `is T` and `json<User>()` work; the interpreter passes type descriptors, LLVM later specialises | PDF / P9 (call syntax) / P9-default (reified) |
| Memory | Automatic reference counting, no GC pauses. Manual memory only inside `unsafe` blocks | PDF |
| Printing | `print` is a built-in function, not a keyword. It takes exactly one argument and writes it plus a newline; `print()` and `print(a, b)` are the error `print takes exactly one argument`. Floats print like Python's `repr`: the shortest digits that read back to the same value, in plain notation with a decimal point when the decimal exponent is between -4 and 15 (`3.0`, `0.0025`, `123456789.123`, `1000000000000000.0`), and in exponent notation otherwise (`1e+16`, `2.5e-05`, `5e-324`). `-0.0`, `inf`, `nan` print as written. `true`, `false` and `null` print as those words. Lists print as `[1, 2, 3]`; strings inside lists print with double quotes, `["a", "b"]` | P1-default / P6-default (float format) |

### 2.5 Statements and expressions

| Area | Decision | Status |
|---|---|---|
| `if` | Statement: `if cond:` / `elif cond:` / `else:`. The condition must be `bool`; `if 1:` is an error | PDF (if) / P1-default (`elif`) / P6-default (bool condition) |
| Conditional expression | `a if cond else b`, as in Python | P1 |
| `when` | Pattern matching, as in Kotlin. Works as a statement and as a value, but as a value only as the complete right-hand side of `let`, `var`, `=` or after `return`; never inside brackets and never as an operand. Branches: `value ->`, `v1, v2 ->`, `in 3..10 ->`, `is Type ->`, `null ->`, `else ->`; inside an `is Type` branch the subject has that type. Without a subject, branches are conditions. After `->` comes one expression or one simple statement (`return x`, `throw e`, `break`, `print(...)`, an assignment); a longer branch ends its line with `->` and continues as an indented block. `when` used as a value needs an expression in every branch (in a block, its last line). A missing branch is a runtime error until exhaustiveness checking arrives (phase 25) | PDF (when) / P1 (value position) / P1-default (branch spellings as in the examples) / P9 (range pattern `in a..b`, branch bodies, block branches) |
| Loops | `while cond:`; `for x in iterable:`; `break`, `continue`. The loop variable of `for` is a fresh immutable binding on every iteration (a closure made inside the loop keeps its own), and in phase 7 it is a single name or `_`. `break`/`continue` outside a loop and `return` outside a function are compile errors | PDF / P7-default (per-iteration variable, errors) |
| Ranges | `a..b` excludes `b`, `a..=b` includes `b`; both ends are `int`. A range is a value: `let r = 0..3` and `print(r)` (shows `0..3`) work; `==` is true when two ranges hold the same ints (`0..3 == 0..=2`, and every empty range equals every other). `..` sits between the comparison and `+` levels and does not chain | P7 (spelling) / P7-default (value, levels) |
| Functions | `fn name(a: int, b: string = "x") -> T:`; `return`; default values; named arguments at the call site. Omitting `-> T` means the function returns nothing: its call cannot be used as a value (a compile error when the callee is a declared function, a runtime error when it is a function value), and a bare `return` is allowed only there; a function with `-> T` that reaches its end without `return` is a runtime error. A name whose `let` has not run yet (a hoisted function called too early) is the runtime error `` `x` has no value yet ``. Nested `fn` declarations are allowed. Since assignment is a statement, `f(a = 1)` is always a named argument. Functions are visible in their whole scope, so a top-level `fn` may be called above its declaration and two functions may call each other; `let`/`var` are not hoisted. Parameters need a type and are immutable; `_` may appear as a parameter name more than once and is never read; a default value is evaluated at every call and may use the parameters to its left; positional arguments come before named ones; a function name cannot be reassigned or redeclared in its scope. At most 1000 calls may be nested; the 1001st is the runtime error `too many nested calls` | PDF (fn) / P1-default (defaults, named args, no-return form, nested fn) / P7 (visibility) / P7-default (the rest) |
| Lambdas and closures | Functions are values: they can be stored, passed and returned, print as `<fn name>` (`<fn lambda>` for a lambda) and compare equal only to themselves. Closures capture variables by reference: an inner function sees and changes the same variable as its enclosing scope. A lambda is `fn(x) => x * 2`: parameters with optional types, an optional `-> T`, and one expression after `=>` that extends as far right as possible; no default values. A lambda without `-> T` whose body is a call passes on what that call returns, possibly nothing: `fn(x) => print(x)` is a lambda that returns nothing. There is no block lambda: multi-line logic is a nested named `fn`, which is a closure too. `print` is an ordinary function value | P7 (spelling, capture by reference, no block lambda) / P7-default (printing, equality, `print` as a value) |
| Errors | A function that can fail is marked `throws`, with no type after it; the compiler refuses an unhandled call. Only `Error` and its subclasses can be thrown: `throw Error("...")`, `throw IOError("...")`. `try:` / `catch e:` catches everything, `catch e is IOError:` selectively, several `catch` in a row are tried in order; `finally:` runs in every case; there is no `defer`. `e.message`; `print(e)` shows `Error: message` (the class name, then the message). A function that calls a throwing parameter is itself marked `throws`; `rethrows` stays reserved until it is needed | PDF (throws, try/catch) / P1-default (`Error`, `e.message`) / P9 (untyped `throws`, only `Error` values, `finally`, no `defer`) / P9-default (`catch e is T`, printing) |
| Classes | `class Name(params):`, methods inside; constructor parameters become fields. `self` is the object inside a method, always written explicitly (`self.name`), never a parameter; a bare `name` in a method is a local or a global, never a field. Closed by default, `open` allows inheritance. Parents are listed after the header's `:`: `class Dog(name: string): Animal(name), Shape:` (a class with its constructor arguments, interfaces by name), then `:` and the body. A method that replaces an inherited one is written `override fn speak()`; without `override` it is the error `` `speak` already exists in `Animal` ``. `interface Shape:` lists method signatures without bodies. `data class Point(x: int, y: int)`: structural `==`, printed form `Point(x=1, y=2)`, `copy(y = 5)`, destructuring `let (x, y) = p`. `enum Color:` with one value per indented line, `Color.green`, printed as `Color.green`; `sealed class` waits for the need in phase 15 | PDF (class, open, interface, data class) / P1-default (parameters are fields) / P9 (explicit `self`, parent list after `:`, `override`, `enum` spelling, data class names) |
| Modules | One file = one module. Everything private, `pub` exports. `use "./geometry"` imports your own file; the module is then available under the file name without `.rt`, as `geometry.square`; `use "./geometry" as geo` renames it; `use "./geometry" { square, PI }` brings those names in directly (nothing else of the module is visible then). A module's top-level code runs once, at its first import, in dependency order. A cycle of imports is a compile error naming the cycle | PDF (module, pub) / P1-default (`use "./path"`, file-stem name) / P9 (alias, selective import, top-level code once, cycles refused) |
| Packages | `use "name@version" allow [rights]`. Rights: `net`, `fs`, `env`, `exec`, `[]` for none. Exact version pinned, plus a lock file | PDF |
| Concurrency | Lightweight tasks: `spawn f(x)` and channels, as in Go. No async/sync split of functions | PDF / OPEN(26) (channel API) |
| Macros | Hygienic: macro-internal names never collide with yours. Hygiene can be broken only explicitly | PDF / OPEN(26) (call marker) |
| `unsafe` | `unsafe:` block for pointers and manual memory | PDF / OPEN(26) (pointer syntax) |
| Foreign code | `import c "header.h"`, `import py module`, `import js "package"` | PDF / OPEN(19) (rights of foreign code) |
| GPU | `@gpu` on a function runs it on the GPU without CUDA | PDF |
| UI | Declarative UI library, SwiftUI/Compose style, later | PDF |

### 2.6 Unique features and their order

1. Package rights (phase 20). Simplest and most unique: ship first, in 0.1.
2. Permanent reproducibility: version in the file plus lock file (phases 3, 20).
3. Import from any language (phases 34–36). Hardest technically.
4. Time-travel debugging (phase 37).
5. CPU and GPU with one code (phase 38).

### 2.7 Standard library

| Module | Contents | Status |
|---|---|---|
| `io` | `print`; reading a line from the user, candidate name `input()` as in Python | PDF / OPEN(21) |
| `strings` | length, case, split, join, search, slices: methods on `string` (phase 11 fixes the list) | PDF / P9 (methods) |
| `collections` | `List`/`MutableList`, `Map`/`MutableMap`, `Set`/`MutableSet` and their methods (phase 12 fixes the list) | PDF / P9 (two types per container) |
| `math` | `sqrt`, `pow`, `abs`, `min`, `max`, constants | PDF / OPEN(21) |
| `fs` | files and directories; needs the `fs` right | PDF / OPEN(21) |
| `env` | environment variables; needs `env` | PDF / OPEN(21) |
| `process` | run programs; needs `exec` | PDF / OPEN(21) |
| `time` | clock, sleep, formatting | PDF / OPEN(21) |
| `net`, `http` | sockets, HTTP client; need `net` | PDF / OPEN(21) (TLS) |
| `json` | parse and print JSON | PDF / OPEN(21) |

Minimum for release 0.1: `io`, `strings`, `collections`, `json`, `fs`, `http`. Whether the standard library needs a `use` line and how its modules are named (`std.fs`?) is OPEN(19).

### 2.8 Tools

| Tool | Decision | Status |
|---|---|---|
| `arete run`, REPL | phase 3 | PDF |
| REPL: multi-line input (`... `), line editing and history (`~/.arete_history`; linenoise on POSIX, the console editor on Windows) | phase 8 | P8 |
| `--dump-tokens`, `--dump-ast` | phase 4 | P1-default |
| `arete test` | runs `tests/`; phase 22 | PDF |
| `arete fmt` | formatter, idempotent on all examples; phase 22 | PDF |
| Linter | unused variables and the like; phase 22; error vs warning OPEN(22) | PDF |
| `arete install X.Y`, several versions side by side | phase 22 | PDF |
| Package manager (`arete add`, registry, publishing) | phase 20d | PDF |
| `arete build` (native code) | phase 32 | PDF |
| Debugger | basic in phase 28, time travel in phase 37 | PDF |
| VS Code extension | syntax highlighting at 0.1 (phase 24), full LSP in phase 40 | PDF |
| Playground | server-side at release, WASM in phase 40 | PDF |

---

## 3. Lexical structure in detail

Tokens the lexer produces, in the order they become relevant:

| Phase | Tokens |
|---|---|
| 3 | `HEADER` (line 1, two integers), `IDENT`, `INT`, `STRING` (escapes `\n \t \\ \" \' \u{...}` processed), `( ) ,`, `NEWLINE`, `EOF`. Comments and blank lines produce no tokens |
| 4 | `FLOAT`, `+ - * / // **` (`%` pending), `.` |
| 5 | `let var = : < > \| ? [ ] fn -> throws` plus compound assignment `+= -= *= /= //=`; words reserved for later phases are the token `RESERVED` |
| 6 | `INDENT`, `DEDENT`, `if elif else and or not true false`, `== != < <= > >=` |
| 7 | `while for in break continue return => .. ..=`; `fn` and `->` in expressions |
| 11 | String parts for interpolation: `STRING_START`, `STRING_PART`, `INTERP_START`, `INTERP_END`, `STRING_END` |
| 12 | `{ }` as Map literal delimiters |
| 13 | `when is null` |

Rules that hold from phase 3 on:

- Every token carries `line` and `column` (1-based). Error messages use them.
- A logical line is a physical line, except that inside `( [ {` line breaks are ignored, so one logical line may span several physical lines. There is no backslash continuation. `NEWLINE` is emitted at the end of a logical line. Blank lines and lines holding only a comment produce no tokens and do not affect indentation.
- The indentation stack starts at 0. A line indented deeper than the top of the stack by exactly 4 emits `INDENT`; shallower lines emit one `DEDENT` per level popped. Any other depth: `error: inconsistent indentation`. The lexer does not know whether a block was expected; the parser reports `unexpected indentation` when an `INDENT` arrives where a statement should start, which also lets `if x` without `:` get its own message.
- A tab in leading whitespace: `error: tab in indentation`, hint `use 4 spaces`.
- A number is `INT` unless it contains `.` followed by a digit, or an exponent. `1..10` is `INT DOTDOT INT`.
- The lexer never produces a `>>` token. When the type parser expects `>` and sees `>=` (as in `List<int>=[]` written without spaces) it consumes one `>` and leaves `=` as the next token.
- An empty file reports `missing version header` at position `1:1`.
- Limits that keep every recursive pass inside the C stack: at most 64 nested parentheses, 63 nested blocks, and expressions at most 1000 operators deep (nesting and one operator chain both count). Each is a clear error, never a crash.

Operator precedence, lowest to highest. Each later phase that adds an operator inserts a row here instead of inventing one.

| Level | Operators | Notes |
|---|---|---|
| 1 | lambda body after `=>` | extends as far right as possible |
| 2 | `a if c else b` | right-associative; the middle operand may not be a bare conditional |
| 3 | `or` | |
| 4 | `and` | |
| 5 | `not` | prefix; it cannot be the operand of a higher level: `1 == not x` is an error, write `1 == (not x)` |
| 6 | `== != < <= > >= in is` | non-associative: `a < b < c` is an error |
| 7 | `??` | right-associative |
| 8 | `.. ..=` | non-associative |
| 9 | `+ -` | |
| 10 | `* / // %` | |
| 11 | unary `-` | |
| 12 | `**` | right-associative; its right operand may start with unary `-` |
| 13 | `() [] . ?.` | postfix: call, index, member, safe member |

---

## 4. Error message format

Every compiler and runtime error has the same shape. This is a feature of the
language, not a detail: the first thing a new user sees is an error.

```
error: <one-line summary>
  --> <file>:<line>:<column>
   |
 7 | <the source line>
   |         ^ <what is wrong here>
   = help: <how to fix it>
```

Decided messages so far:

| Situation | Summary | Help |
|---|---|---|
| No header | `missing version header` | `` add `arete 0.1` at the top of the file `` |
| `arete` without version | `version required` | `usage: arete <version>`, `installed: 0.1` |
| Unknown version | `Arete 2.0 is not installed` | `run: arete install 2.0` |
| Assign to `let` | `` cannot assign to immutable `x` `` | `` declare it with `var` if it must change `` |
| Tab in indentation | `tab in indentation` | `use 4 spaces` |
| Bad indentation | `inconsistent indentation` | expected depth |
| Package right missing | `package "http@2.1" requires [net, fs], but only [net] allowed` (lists everything the package needs) | the corrected `use` line |
| Int overflow | `` integer overflow in `+` `` (the caret marks the operator) | runtime, with position |
| File needs another version | `file requires Arete 1.0, this is Arete 0.1` | `run: arete install 1.0` |
| Line 1 is not `arete <int>.<int>` | `malformed version header` | `` the first line must be exactly `arete 0.1` `` |
| Indent after a line without `:` | `unexpected indentation` | `` the previous line does not end with `:` `` |
| Unterminated string | `unterminated string literal` | points at the opening quote |
| Unknown character | `` unexpected character `§` `` | |
| `print()` or `print(a, b)` | `print takes exactly one argument` | `write print(x)` |
| `print(print(x))` | `` `print` returns nothing and cannot be used as a value `` | `call it on its own line` |
| `shout(x)` | `` unknown function `shout` `` | `` declare it first: `fn shout(...):` ``; when a `let` of that name follows further down, the label names its line and the help says only functions are visible before their declaration |
| a name whose `let` has not run (REPL after a failed declaration, or a hoisted call) | `` `x` has no value yet `` | `` declare `x` before the code that reads it `` |
| `let print = 5` | `` `print` is already declared `` | `it is a built-in function`, `choose another name` |
| `print(x = 1)` | `` `print` has no parameter `x` `` | `write print(x)` |
| `print("a",)` | `` expected an argument after `,`, found `)` `` | `remove the trailing comma` (same for a parameter list) |
| `fn class():` | `` `class` is a reserved word `` | |
| `fn 1():` | `` expected a function name after `fn`, found number `` | |
| `fn(x) =>` with nothing after | `` expected an expression after `=>`, found end of line `` | `a lambda body is one expression` |
| `fn f(x: int = ):` | `` expected a default value, found `)` `` | |
| `print(` at end of file | `` unclosed `(` `` | `` add the matching `)` `` |
| `)` without `(` | `` unmatched `)` `` | |
| `\q` in a string | `unknown escape sequence` | `valid escapes: \n \t \\ \" \' \u{...}` |
| `print "x"` | `expected end of line, found string` | `a call needs parentheses: print("...")` |
| `a < b < c` | `chained comparison is not allowed` | `` write `a < b and b < c` `` |
| `x if c` without `else` | `conditional expression needs else` | |
| Lone `}` in a string | `single } in string` | `write }}` |
| `int \| null` | `null is not a type name` | `write int?` |
| `1 / 0`, `1 // 0` | `division by zero` | runtime, with position |
| `"a" + 1`, `"a" < 1` | `` cannot apply `+` to string and int `` | runtime until phase 10 |
| `if 1:` | `` the condition of `if` must be bool, found int `` | `` compare explicitly, for example `x != 0` `` |
| `not 1` | `` operand of `not` must be bool, found int `` | `` compare explicitly, for example `x != 0` `` |
| `1 and true` | `` left side of `and` must be bool, found int `` (also `right side of`, and `or`) | same |
| `1 == not x` | `` `not` cannot be an operand here `` | `` add parentheses: `a == (not b)` `` |
| 1000 operators deep | `expression nested too deeply` | `` split it into smaller steps with `let` `` |
| `print(_)` | `` `_` cannot be read `` | give it a real name |
| `true \| false` | `` expected `)`, found `|` `` | `` `|` only separates types; for logic write `or` `` |
| `let x int = 5` | `` expected `=` or `:`, found identifier `` | `` a type annotation is written `let x: int = 5` `` |
| `let x = 1` twice | `` `x` is already declared `` | the line of the first declaration |
| `let x` | `` `let` needs a value `` | `` write `let x = value` `` |
| `let class = 1` | `` `class` is a reserved word `` | choose another name |
| `if x` without `:` | `` expected `:`, found end of line `` | `` write `if condition:` `` |
| `if x:` with nothing below | `` expected an indented block after `if` `` | |
| `elif` after `else` | `` `elif` without a matching `if` `` | |
| `else x:` | `` `else` takes no condition `` | |
| `9223372036854775808` | `integer literal is too large` | |
| `12abc` | `invalid number literal` | |
| `{` in a string before phase 11 | `string interpolation comes in phase 11` | |

| `"x"("y")` | `a string cannot be called` (likewise `an int`, `a float`, `a bool`) | `to call a function write its name: f(...)` |
| `x(1)` where `x` holds an int | `` `x` is not a function `` | runtime; `cannot call int` when the callee is not a name |
| `break` outside a loop | `` `break` outside a loop `` (also `continue`) | `` `break` only works inside `while` or `for` `` |
| `return` at top level | `` `return` outside a function `` | |
| `return 1` in a function without `-> T` | `` `say` returns nothing `` | `` add `-> type` after the parameters, or write a bare `return` `` |
| bare `return` in a function with `-> T` | `` `f` must return a value `` | `` write `return value` `` |
| a function with `-> T` reaches its end | `` `f` ended without returning a value `` | runtime, points at the call |
| `f(1)` with a required `b` | `` missing argument `b` in call to `f` `` | `pass it by position or by name: b = ...` |
| `f(1, 2)` for a one-parameter `f` | `` too many arguments to `f` `` | the label says how many it takes |
| `f(c = 1)` with no parameter `c` | `` `f` has no parameter `c` `` | lists the parameters |
| `f(1, a = 2)` | `` argument `a` is given twice `` | |
| `f(a = 1, 2)` | `positional argument after a named one` | `named arguments go last: f(1, 2, name = 3)` |
| `let v = say()` for a function without `-> T` | `` `say` returns nothing and cannot be used as a value `` | `call it on its own line` |
| `fn h(x):` | `` parameter `x` needs a type `` | `` write `x: int` `` |
| `fn h(x: int) => x` | `a named function has a block body` | `` `=>` is for lambdas `` |
| `fn(x = 1) => x` | `a lambda parameter cannot have a default value` | |
| 1001 nested calls | `too many nested calls` | runtime; `the limit is 1000 nested calls` |
| `for i in 5:` | `cannot iterate over int` | `` write a range: `for i in 0..10:` `` |
| `0..1.5` | `range ends must be int, found float` | compile time (runtime for a value the checker could not see) |
| `var x = 5` then `x = "a"`; a wrong argument, return, default or annotation | `expected int, found string` | the label says where each type comes from (`` `x` is int ``, `this argument is string`, `returned here`) |
| `n /= 2` on an int | `expected int, found float` | `` use `//=` for integer division `` |
| `1 if c else "a"` | `branches of a conditional have different types: int and string` | `give both branches the same type` |
| `1 == "a"` | `` cannot apply `==` to int and string `` | `the two sides can never be equal` |
| `fn(x) => x` with no context | `` parameter `x` needs a type `` | `` write `fn(x: int) => ...`, or pass the lambda where a `fn(int) -> T` is expected `` |
| a lambda with the wrong number of parameters | `expected fn(int) -> int, found a lambda with 2 parameters` | |
| `let x: Foo = 1` | `` unknown type `Foo` `` | `the types so far are int, float, string, bool and fn(...) -> T` |
| `let xs: List<int> = ...` before phase 12 | `` collection types like `List<int>` arrive in phase 12 `` (likewise `int?` phase 16, unions phase 13, generics phase 17, `throws` in a type phase 14) | |
| `2 ** -1` | `negative exponent for an int base` | `` write the base as a float: `2.0 ** -1` `` |
| `1..2..3` | `a range cannot be an end of another range` | |
| C stack exhausted | `stack overflow` | never a crash: the walk checks its stack use |
| `arete 0.1` on line 2 or in the REPL | `version header belongs on line 1 only` | |
| `arete run a.rt extra` | `` unexpected argument `extra` `` | usage |
| stdout cannot be written | `cannot write output` | exit code 1 |

The column in the `-->` line counts bytes from the start of the line, which is what editors expect. The caret is placed by characters, so it lands under the right symbol after tabs and non-ASCII text. Lines longer than 120 bytes are shown as a window around the error, marked with `...`.

Comments in example files may abbreviate this block. A `.expected_error` file holds only the summary line; the test runner checks that stderr contains it.

---

## 5. Syntax by construct

Decided forms are shown as they will be parsed. Provisional forms carry an
`OPEN(n)` marker and may change in phase n. All forms appear in
`examples/pending/`.

### Program

```
arete 0.1
# comment

let greeting = "Hello"
print(greeting)
```

### Variables and types

```
let name = "Arete"            # inferred string, immutable
let year: int = 2026          # explicit annotation
var count = 0                 # mutable
count += 1
let maybe: int? = null        # nullable
let either: int | string = 5  # union
let xs: List<int> = []        # generic
```

Type grammar (parsed from phase 5; checked from phase 10 if the incremental
type checker is chosen there, see OPEN(10)):

```
Type     = Union ;
Union    = Postfix { "|" Postfix } ;
Postfix  = Primary { "?" } ;
Primary  = NAME [ "<" Type { "," Type } ">" ]
         | "fn" "(" [ Type { "," Type } ] ")" [ "->" Type ] [ "throws" [ NAME ] ]
         | "(" Type ")" ;
```

Reading rules: the result type of a `fn` type extends as far right as possible,
so `fn() -> int?` is a function returning `int?`; a nullable function type is
written `(fn() -> int)?`. `throws` binds to the innermost `fn`: in
`fn make() -> fn() -> int throws:` the returned function throws, not `make`;
write `-> (fn() -> int) throws:` for the other meaning. `null` is not a type
name.

### Conditions

```
if x < 0:
    print("negative")
elif x == 0:
    print("zero")
else:
    print("positive")

let sign = "neg" if x < 0 else "pos"
```

### Loops

```
while i < 3:
    i += 1

for n in 0..3:        # 0, 1, 2
    print(n)
for n in 1..=3:       # 1, 2, 3
    print(n)
for item in items:    # collections: phase 12
    print(item)
for i in 0..10:
    if i == 2:
        continue
    if i == 5:
        break
```

### Functions

```
fn greet(name: string, greeting: string = "Hello") -> string:
    return greeting + ", " + name + "!"

print(greet("Karim", greeting = "Hi"))

let double = fn(x: int) => x * 2          # a lambda: one expression after `=>`
fn apply(f: fn(int) -> int, v: int) -> int:
    return f(v)
print(apply(fn(x) => x + 1, 9))           # parameter types may be left out in a lambda

fn make_counter() -> fn() -> int:          # a closure captures `count` by reference
    var count = 0
    fn next() -> int:
        count += 1
        return count
    return next

main()                                     # functions are visible in their whole scope
fn main():
    print(fib(20))
fn fib(n: int) -> int:
    return n if n < 2 else fib(n - 1) + fib(n - 2)
```

### Strings

```
print("Hi, {name}! Next year: {year + 1}")
print("literal braces: {{x}}")
print('single "quotes" inside')
print(r"raw: no {interpolation} and no \n")
print(name.len())                      # methods, never len(name)
print(name.upper())
print("ab" * 3)                        # abab ab
print("ar" in name)                    # substring test
print("age " + str(42))
```

### Collections

```
var nums: MutableList<int> = [3, 1, 2]  # the literal takes the expected type
nums.add(4)
nums.sort()
let fixed = [1, 2, 3]                  # List<int>: no add, no sort
let ages = {"ann": 31, "bob": 25}      # Map<string, int>
print(ages["ann"])
print("bob" in ages)
for (name, age) in ages:               # destructuring a Map entry
    print(name + " " + str(age))
var empty = {}                         # an empty Map
var tags: MutableSet<string> = Set()   # a set is always spelled with its name
```

### when

```
let word = when x:
    0 -> "zero"
    1, 2 -> "one or two"
    in 3..10 -> "small"
    else -> "big"

when v:                        # v: int | string
    is int -> print("int")
    is string -> print("string")

when:                          # no subject
    n < 0 -> print("negative")
    else -> print("other")

when command:                  # a branch may be one statement, or a block
    "quit" -> return
    "help" ->
        print("commands:")
        print("quit, help")
    else -> count += 1
```

### Errors

```
fn divide(a: int, b: int) -> int throws:
    if b == 0:
        throw Error("division by zero")
    return a // b

try:
    print(divide(1, 0))
catch e is IOError:
    print("io: " + e.message)
catch e:
    print("caught: " + e.message)   # prints: caught: division by zero
    print(e)                        # prints: Error: division by zero
finally:
    print("always")
```

### Classes

```
open class Animal(name: string):
    fn speak() -> string:
        return self.name + " makes a sound"      # fields always through self

class Dog(name: string): Animal(name):          # parents after the header's `:`
    override fn speak() -> string:              # override is required
        return self.name + " barks"

interface Shape:
    fn area() -> float

class Square(side: float): Shape:
    fn area() -> float:
        return self.side * self.side

data class Point(x: int, y: int)
let (x, y) = Point(1, 2)                        # destructuring, by position

enum Color:
    red
    green
```

### Modules and packages

```
use "./geometry"                       # geometry.square
use "./geometry" as geo                # geo.square
use "./geometry" { square, PI }        # square(4), PI
use "json@1.4" allow []                # pure computation
use "http@2.1" allow [net]             # network only
use "db@3.0"   allow [net, fs]         # network and files
```

### Concurrency, macros, unsafe, foreign code, GPU

```
spawn worker(1, results)               # OPEN(26): channel API
macro swap(a, b): ...                  # OPEN(26): call marker
unsafe: ...                            # OPEN(26): pointer syntax
import py numpy as np                  # OPEN(19): rights
@gpu
fn add(a: List<float>, b: List<float>) -> List<float>: ...
```

---

## 6. Open decision queue

Nothing here is forgotten; each item is decided in the phase named, with
examples, before any code for it is written.

**Phase 3 (first code), decided P3-default:** one binary with a built-in version table; a launcher over `~/.arete/versions/` replaces the table in phase 22 without changing the command line.

**Phase 5 (variables), decided P5-default:** a second `let x` in the same scope is an error; allowed in the REPL. Type annotations are parsed and kept but not checked until phase 10.

**Phase 7 (loops, functions), decided P7:** ranges are `a..b` and `a..=b`; lambdas are `fn(x) => expr` with optional parameter types and `-> T`, and there is no block lambda (a nested named `fn` serves); closures capture by reference; `fn` are visible in their whole scope. P7-default: `not in` is one operator, implemented with collections in phase 12.

**Phase 9 (stage C syntax session), decided P9 on 2026-10-22:** methods only on strings and collections (`s.len()`), `*` repeats and `in` finds substrings; `List`/`MutableList` and the same pair for `Map` and `Set`, a literal takes the expected type, `{}` is an empty `Map`, sets are spelled `Set()`; `when` branches hold an expression or one statement, a block follows `->` at the end of a line, range patterns are `in a..b`; `throws` carries no type, only `Error` values are thrown, `finally:` exists and `defer` does not; explicit `self`; parents listed after the class header's `:`, `override` required, `enum` with one value per line; explicit type arguments `f<T>()` with the lookahead rule; `use ... as`, `use ... { names }`, top-level module code runs once, import cycles are an error; destructuring `let (x, y) = p` and `for (k, v) in m`. P9-default: `str()`, raw strings `r"..."`, reified generics, `catch e is T`, `print(e)` as `Error: message`, method names (`add`, `remove`, `len()`, `keys()`, `sorted()`/`sort()`), `sealed` and `rethrows` wait for a need. The pending examples 08–15 use this syntax.

**Phase 10 (types), decided P9 and P10:** the incremental type checker started in phase 10 (path B of the ROADMAP): `int`, `float`, `string`, `bool`, `fn`, `range`, inference for `let`, function signatures, lambda parameters from context; every later phase adds the types of its feature. P10: `int ** int` is `int` (negative exponent is a runtime error), `int` is accepted where `float` is expected, `==` between unrelated types is a compile error.

**Phase 19 (rights session):** right names; static (lexical) vs dynamic rights model; transitive rights; source of a package's requirements; standard library naming; rights of foreign code.

**Phase 26 (runtime session):** cycles under reference counting, and the fact that a cycle collector means short pauses, which revises the PDF promise "no GC pauses"; deterministic destructors; task scheduler model; does `spawn` return a handle, is there `join`, what happens to `throws` inside `spawn`; channel API and `select`; macro call marker; pointer syntax (`*int` collides with `**`); VM architecture; dependency policy.

---

## 7. Example programs

All in `examples/pending/`. A file moves to `examples/` with a generated `.expected` file when its phase is reached and it passes (see section 2.2).

| # | File | Shows | Runs after phase |
|---|---|---|---|
| 01 | `01_hello.rt` | header, `print` | 3 |
| 02 | `02_arithmetic.rt` | numbers, operators, precedence | 4 |
| 03 | `03_variables.rt` | `let`, `var`, annotations, `+=`, `_` in numbers | 5 |
| 04 | `04_conditions.rt` | `if/elif/else`, `and/or/not`, conditional expression | 6 |
| 05 | `05_loops.rt` | `while`, `for`, ranges, `break`, `continue` | 7 |
| 06 | `06_functions.rt` | `fn`, defaults, named args, recursion | 7 |
| 07 | `07_closures.rt` | functions as values, closures, lambdas | 7 |
| 08 | `08_strings.rt` | interpolation, escapes, methods, slices | 11 |
| 09 | `09_collections.rt` | `List`, `Map`, `Set` | 12 |
| 10 | `10_when.rt` | `when` in all forms, union types | 13 |
| 11 | `11_errors.rt` | `throws`, `throw`, `try/catch` | 14 |
| 12 | `12_classes.rt` | classes, `open`, interfaces, data classes, enum | 15 |
| 13 | `13_null_safety.rt` | `?`, `??`, `?.`, smart casts | 16 |
| 14 | `14_generics.rt` | generic functions and classes, constraints | 17 |
| 15 | `15_modules/` | two files, `use "./x"`, `pub` | 18 |
| 16 | `16_packages.rt`, `16b_packages_denied.rt` | `use "x@v" allow [...]`, rights error | 20 |
| 17 | `17_spawn.rt` | `spawn`, channels | 30 |
| 18 | `18_macros.rt` | hygienic macro | 31 |
| 19 | `19_import_any.rt` | `import c/py/js`, `unsafe` | 36 |
| 20 | `20_gpu.rt` | `@gpu` | 38 |
