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
| Implementation language | C11, GNU make, no dependencies beyond libc. Compiles with gcc, clang, MSVC | PDF (C) / P1-default (portability) |
| Audience | Developers who share loudly: vibe-coders and Python developers who need speed | PDF |
| Release | GitHub, MIT, builds for Linux/macOS/Windows via GitHub Actions, one-command install, docs site, VS Code extension, semver (0.1 then 1.0), a flagship project written in Arete, an online playground, announcement on Hacker News, r/ProgrammingLanguages, Discord, Twitter/X, YouTube, Reddit | PDF / OPEN(24) (what the flagship project is) |
| Execution | Tree-walking interpreter and REPL for development (phase 3), bytecode VM (phase 27), native code via LLVM for release with `arete build` (phase 32). All three must give identical output on `tests/` | PDF / OPEN(26) (VM architecture) |
| Command line | `arete run <file.rt>` runs a file. `arete <version>` starts the REPL: banner `Arete 0.1 REPL`, prompt `>>> `, continuation prompt `... `; when stdin is not a terminal there is no banner and no prompt. All errors go to stderr; the exit code is 1 on any error, 0 otherwise; the REPL keeps running after an error and exits 1 at the end if any line failed. Extra command-line arguments are an error. `--dump-tokens` and `--dump-ast` arrive in phase 4 | PDF (run, REPL) / P1-default (stderr, exit code, no-terminal rule) / P3-default (REPL exit code, extra arguments) |

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
| `_` | A lone underscore is the discard name: `for _ in 1..=3:`. It cannot be read | P1-default |
| Logical operators | The words `and`, `or`, `not`. Short-circuit | P1 |
| Comparison | `== != < <= > >=` | PDF-implied |
| Arithmetic | `+ - * / // % **`. `/` on two ints gives a `float`; `//` is floor division; `%` is remainder with the sign of the divisor (Python semantics); `**` is power, right-associative. Unary minus is an operator, not part of the number: `-7 // 2` is `(-7) // 2`, which is `-4`; `-2 ** 2` is `-(2 ** 2)`, which is `-4` (Python). Full precedence table in section 3 | P1 (`/`, `//`) / P1-default (`%` sign, `**`, unary minus) |
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
| Typing | Static, inferred: `let x = 5` has type `int`. Annotations optional: `let x: int = 5` | PDF |
| Base types | `int` (64-bit signed), `float` (64-bit), `string` (immutable, UTF-8), `bool`, `null` | PDF names / P1-default sizes |
| Integer overflow | Runtime error. Wrapping operations come later as library functions. A `bigint` type may come in the standard library | P1 |
| Union types | `int \| string`, as in TypeScript | PDF |
| Nullability | Forbidden by default. `string?` is the nullable string type and the only spelling: `string \| null` is an error with hint `write string?`. `??` gives a default, `?.` is a safe call. Checks are static (compile time) | PDF (`?`, `??`) / P1-default (`?.`, single spelling) |
| Strings | Immutable. `s[i]` is a one-character string (there is no `char` type); `s[a..b]` is a slice with the end excluded. Methods vs functions (`s.len()` vs `len(s)`), repeat with `*`, substring test with `in`: open | P1-default (indexing, slicing, no `char`) / OPEN(9) (methods, repeat, `in`) |
| Collections | `List<T>` literal `[1, 2]`; `Map<K, V>` literal `{"a": 1}`, iterated in insertion order, `m[k]` and `k in m`; `Set<T>`. An empty generic container is written with an annotation, `var s: Set<string> = Set()`, so no example needs explicit type arguments at a call. Mutability of a `let`-bound collection, `List` vs `MutableList`, method names, the meaning of empty `{}`: open | P1-default (literals, insertion order, annotated empty form) / OPEN(9) (mutability, names, empty `{}`) |
| Variables | `let` immutable (default), `var` mutable. Assigning to a `let` is a compile error | PDF |
| Generics | `List<T>`, `fn first<T>(xs: List<T>) -> T`, `class Box<T>`, constraints `T: Comparable`. Explicit type arguments at a call (`json<User>()`) collide with `<` as comparison; the spelling is decided in phase 9, and the examples that need it are marked OPEN | PDF / OPEN(9) (explicit type arguments, reified vs erased) |
| Memory | Automatic reference counting, no GC pauses. Manual memory only inside `unsafe` blocks | PDF |
| Printing | `print` is a built-in function, not a keyword. It takes exactly one argument and writes it plus a newline; `print()` and `print(a, b)` are the error `print takes exactly one argument`. Floats print as the shortest decimal that reads back to the same value, always with a decimal point (`3.0`, `3.14159`). `true`, `false` and `null` print as those words. Lists print as `[1, 2, 3]`; strings inside lists print with double quotes, `["a", "b"]` | P1-default |

### 2.5 Statements and expressions

| Area | Decision | Status |
|---|---|---|
| `if` | Statement: `if cond:` / `elif cond:` / `else:` | PDF (if) / P1-default (`elif`) |
| Conditional expression | `a if cond else b`, as in Python | P1 |
| `when` | Pattern matching, as in Kotlin. Works as a statement and as a value, but as a value only as the complete right-hand side of `let`, `var`, `=` or after `return`; never inside brackets and never as an operand. Branches: `value ->`, `v1, v2 ->`, `in range ->`, `is Type ->`, `null ->`, `else ->`; inside an `is Type` branch the subject has that type. Without a subject, branches are conditions. A missing branch is a runtime error until exhaustiveness checking arrives (phase 25) | PDF (when) / P1 (value position) / P1-default (branch spellings as in the examples) / OPEN(9) (range pattern spelling, branch bodies, block branches) |
| Loops | `while cond:`; `for x in iterable:`; `break`, `continue` | PDF |
| Ranges | `a..b` excludes `b`, `a..=b` includes `b` | OPEN(7), provisional in examples |
| Functions | `fn name(a: int, b: string = "x") -> T:`; `return`; default values; named arguments at the call site. Omitting `-> T` means the function returns nothing: its call cannot be used as a value, and a bare `return` is allowed only there. Nested `fn` declarations are allowed. Since assignment is a statement, `f(a = 1)` is always a named argument | PDF (fn) / P1-default (defaults, named args, no-return form, nested fn) |
| Lambdas and closures | Functions are values; closures capture variables (the examples assume capture by reference). Spelling of lambdas is open; examples use `fn(x) => x * 2`. A block lambda inside parentheses conflicts with the rule that brackets suppress indentation, so phase 7 chooses between: no block lambdas (use a nested named `fn`), a trailing block after the closing parenthesis, or a lexer exception | OPEN(7) |
| Errors | A function that can fail is marked `throws`; the compiler refuses an unhandled call. `throw Error("...")`, `try:` / `catch e:`, `e.message` | PDF (throws, try/catch) / P1-default (`Error`, `e.message`) / OPEN(9) (typed throws, `finally`/`defer`, printing an `Error` value) |
| Classes | `class Name(params):`, methods inside; constructor parameters become fields; `self`; closed by default, `open` allows inheritance; `interface`; `data class` with structural `==`, a printed form `Point(x=1, y=2)` and `copy`; `enum` | PDF (class, open, interface, data class) / P1-default (parameters are fields) / OPEN(9) (`self` vs `this`, inheritance and interface spelling, `override`, data class method names and print format, `enum`) |
| Modules | One file = one module. Everything private, `pub` exports. `use "./geometry"` imports your own file; the module is then available under the file name without `.rt`, as `geometry.square` | PDF (module, pub) / P1-default (`use "./path"`, file-stem name) / OPEN(9) (alias, selective import, top-level code, cycles) |
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
| `strings` | length, case, split, join, search, slices | PDF / OPEN(9) (names) |
| `collections` | `List`, `Map`, `Set` and their methods | PDF / OPEN(9) (names) |
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
| 4 | `FLOAT`, `+ - * / // % **`, `,` |
| 5 | `let var = : < > \| ? [ ] . fn ->` (the last three only inside type annotations) plus compound assignment |
| 6 | `INDENT`, `DEDENT`, `if elif else and or not true false`, comparisons |
| 7 | `while for in break continue return => .. ..=`; `fn` and `->` in expressions |
| 11 | String parts for interpolation: `STRING_START`, `STRING_PART`, `INTERP_START`, `INTERP_END`, `STRING_END` |
| 12 | `{ }` as Map literal delimiters |
| 13 | `when is null` |

Rules that hold from phase 3 on:

- Every token carries `line` and `column` (1-based). Error messages use them.
- A logical line is a physical line, except that inside `( [ {` line breaks are ignored, so one logical line may span several physical lines. There is no backslash continuation. `NEWLINE` is emitted at the end of a logical line. Blank lines and lines holding only a comment produce no tokens and do not affect indentation.
- The indentation stack starts at 0. A line indented deeper than the top of the stack by exactly 4 emits `INDENT`; shallower lines emit one `DEDENT` per level popped. Any other depth: `error: inconsistent indentation`. A deeper line after a line that does not end with `:` is `error: unexpected indentation`.
- A tab in leading whitespace: `error: tab in indentation`, hint `use 4 spaces`.
- A number is `INT` unless it contains `.` followed by a digit, or an exponent. `1..10` is `INT DOTDOT INT`.
- The lexer never produces a `>>` token. When the type parser expects `>` and sees `>=` (as in `List<int>=[]` written without spaces) it consumes one `>` and leaves `=` as the next token.
- An empty file reports `missing version header` at position `1:1`.

Operator precedence, lowest to highest. Each later phase that adds an operator inserts a row here instead of inventing one.

| Level | Operators | Notes |
|---|---|---|
| 1 | lambda body after `=>` | extends as far right as possible |
| 2 | `a if c else b` | right-associative; the middle operand may not be a bare conditional |
| 3 | `or` | |
| 4 | `and` | |
| 5 | `not` | prefix |
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
| Int overflow | `` integer overflow in `a + b` `` | runtime, with position |
| File needs another version | `file requires Arete 1.0, this is Arete 0.1` | `run: arete install 1.0` |
| Line 1 is not `arete <int>.<int>` | `malformed version header` | `` the first line must be exactly `arete 0.1` `` |
| Indent after a line without `:` | `unexpected indentation` | `` the previous line does not end with `:` `` |
| Unterminated string | `unterminated string literal` | points at the opening quote |
| Unknown character | `` unexpected character `§` `` | |
| `print()` or `print(a, b)` | `print takes exactly one argument` | `write print(x)` |
| `print(print(x))` | `` `print` returns nothing and cannot be used as a value `` | `call it on its own line` |
| `shout(x)` | `` unknown function `shout` `` | `` only `print` exists so far `` (phase 3) |
| `print(` at end of file | `` unclosed `(` `` | `` add the matching `)` `` |
| `)` without `(` | `` unmatched `)` `` | |
| `\q` in a string | `unknown escape sequence` | `valid escapes: \n \t \\ \" \' \u{...}` |
| `print "x"` | `expected end of line, found string` | `a call needs parentheses: print("...")` |
| `a < b < c` | `chained comparison is not allowed` | `` write `a < b and b < c` `` |
| `x if c` without `else` | `conditional expression needs else` | |
| Lone `}` in a string | `single } in string` | `write }}` |
| `int \| null` | `null is not a type name` | `write int?` |
| `{` in a string before phase 11 | `string interpolation comes in phase 11` | |

| `"x"("y")` | `only a name can be called` | |
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

for n in 0..3:        # 0, 1, 2            OPEN(7)
    print(n)
for n in 1..=3:       # 1, 2, 3            OPEN(7)
    print(n)
for item in items:
    print(item)
```

### Functions

```
fn greet(name: string, greeting: string = "Hello") -> string:
    return greeting + ", " + name + "!"

print(greet("Karim", greeting = "Hi"))

let double = fn(x: int) => x * 2          # OPEN(7)
fn apply(f: fn(int) -> int, v: int) -> int:
    return f(v)
```

### Strings

```
print("Hi, {name}! Next year: {year + 1}")
print("literal braces: {{x}}")
print('single "quotes" inside')
```

### when

```
let word = when x:
    0 -> "zero"
    1, 2 -> "one or two"
    in 3..10 -> "small"       # OPEN(9)
    else -> "big"

when v:                        # v: int | string
    is int -> print("int")
    is string -> print("string")

when:                          # no subject
    n < 0 -> print("negative")
    else -> print("other")
```

### Errors

```
fn divide(a: int, b: int) -> int throws:
    if b == 0:
        throw Error("division by zero")
    return a // b

try:
    print(divide(1, 0))
catch e:
    print("caught: " + e.message)
```

### Classes

```
open class Animal(name: string):
    fn speak() -> string:
        return self.name + " makes a sound"      # OPEN(9): self vs this

class Dog(name: string): Animal(name):          # OPEN(9): ':' would mean three things
    fn speak() -> string:
        return self.name + " barks"

interface Shape:
    fn area() -> float

data class Point(x: int, y: int)

enum Color:                                     # OPEN(9)
    red
    green
```

### Modules and packages

```
use "./geometry"                       # your own file; OPEN(9): alias
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

**Phase 5 (variables):** shadowing: is a second `let x` in the same file an error (yes in the examples), and allowed in the REPL.

**Phase 7 (loops, functions):** range spelling (`..`/`..=` vs `..<` vs `range()`); lambda spelling, a return type on lambdas, and whether a block lambda exists given that brackets suppress indentation; capture by reference or by value; are top-level `fn` visible before their definition; `not in` as one operator.

**Phase 9 (stage C syntax session):** `when` branch bodies (expression only, or also `return`, `throw`, `break`, assignment) and block branches `-> :`; raw strings without interpolation; printing an `Error` value; data class destructuring; string methods vs `len()`; string repeat `*` and substring `in`; collection literals, empty `{}`, `List` vs `MutableList`, method names (`add`/`append`); destructuring in `for`; `str()` conversion; `when` branch spellings and exhaustiveness; typed `throws`, what may be thrown, `finally`/`defer`, `rethrows`; `self` vs `this`; inheritance and override spelling; interface implementation spelling; data class method names; `enum` and `sealed`; explicit type arguments at call (`json<User>()` vs `json::<User>()`); reified vs erased generics; module alias and selective import; top-level code in modules; circular imports.

**Phase 10 (types):** incremental type checker from phase 10 (recommended) or one block after stage C as in the PDF.

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
