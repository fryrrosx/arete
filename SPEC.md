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
| Name | Arete | PDF |
| File extension | `.rt` | PDF |
| License | MIT | P1 |
| Repository | `github.com/fryrrosx/arete` | P1 |
| Implementation language | C11, GNU make, no dependencies beyond libc. Compiles with gcc, clang, MSVC | PDF (C) / P1-default (portability) |
| Audience | Developers who share loudly: vibe-coders and Python developers who need speed | PDF |
| Release | GitHub, MIT, builds for Linux/macOS/Windows via GitHub Actions, one-command install, docs site, VS Code extension, semver (0.1 then 1.0) | PDF |

### 2.2 Source files and versions

| Area | Decision | Status |
|---|---|---|
| Version header | Line 1 of every file is exactly `arete <major>.<minor>`. Nothing may precede it: no blank line, no comment, no shebang. Missing header is a compile error | PDF (header) / P1 (strict line 1) |
| Header value now | `arete 0.1` during development. The header is the pinned language version. A 0.x toolchain runs only files with exactly the same `0.x` (minor versions break compatibility before 1.0). At the 1.0 release all files are updated once; from 1.0 on, a file `arete 1.a` runs on any toolchain `1.b` with b ≥ a | P1 |
| REPL | Starts only with a version: `arete 0.1`. `arete` alone: `error: version required`, lists installed versions. `arete 2.0` when absent: `error: Arete 2.0 is not installed`, hint `arete install 2.0` | PDF |
| Entry point | Top-level statements run top to bottom, like a Python script. No `main` | P1 |
| Comments | `#` to end of line. No block comments | PDF (examples) / P1-default (no block comments) |
| Example files | Line 2 of every file in `examples/pending/` is `# runs-after: phase N`: the first phase after which it must run. Expected output is in a comment at the end | P1 |
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
| Arithmetic | `+ - * / // % **`. `/` on two ints gives a `float`; `//` is floor division; `%` is remainder with the sign of the divisor (Python semantics); `**` is power, right-associative | P1 (`/`, `//`) / P1-default (`%` sign, `**`) |
| Compound assignment | `+= -= *= /= //= %=` | P1-default |
| Integer literals | Decimal, `_` allowed between digits: `1_000_000`. Hex `0xFF`, binary `0b101` | P1-default |
| Float literals | `1.5`, `1e9`, `2.5e-3`. Neither `1.` nor `.5` is allowed, so `1..10` lexes as `1 .. 10` | P1-default |
| String literals | `"..."` and `'...'` are identical. Escapes: `\n \t \\ \" \' \{ \u{1F600}`. Interpolation `{expr}` is always on; literal braces are written `{{` and `}}` | P1 |
| Booleans, null | `true`, `false`, `null` | P1-default |
| Reserved words | `and as break catch class continue data defer elif else enum false finally fn for if import in interface is let macro not null open or pub rethrows return select self sealed spawn super this throw throws true try unsafe use var weak when while` plus `allow` after `use`. Some are reserved for later phases; using one as a name is an error from day one | P1-default |
| Reserved punctuation | `-> => .. ..= ? ?. ?? @ !` (the `!` directly after an identifier is reserved for a possible macro-call marker, decided in phase 26) | P1-default |

### 2.4 Types and values

| Area | Decision | Status |
|---|---|---|
| Typing | Static, inferred: `let x = 5` has type `int`. Annotations optional: `let x: int = 5` | PDF |
| Base types | `int` (64-bit signed), `float` (64-bit), `string` (immutable, UTF-8), `bool`, `null` | PDF names / P1-default sizes |
| Integer overflow | Runtime error. Wrapping operations come later as library functions. A `bigint` type may come in the standard library | P1 |
| Union types | `int \| string`, as in TypeScript | PDF |
| Nullability | Forbidden by default. `string?` means `string \| null`. `??` gives a default, `?.` is a safe call. Checks are static (compile time) | PDF (`?`, `??`) / P1-default (`?.`) |
| Variables | `let` immutable (default), `var` mutable. Assigning to a `let` is a compile error | PDF |
| Generics | `List<T>`, `fn first<T>(xs: List<T>) -> T`, constraints `T: Comparable` | PDF |
| Memory | Automatic reference counting, no GC pauses. Manual memory only inside `unsafe` blocks | PDF |
| Printing | `print(x)` writes one value and a newline. Floats always print with a decimal point (`3.0`). Lists print as `[1, 2, 3]` | P1-default |

### 2.5 Statements and expressions

| Area | Decision | Status |
|---|---|---|
| `if` | Statement: `if cond:` / `elif cond:` / `else:` | PDF (if) / P1-default (`elif`) |
| Conditional expression | `a if cond else b`, as in Python | P1 |
| `when` | Pattern matching, as in Kotlin. Works as a statement and as a value on the right side of `=` or after `return`. Branches: `value ->`, `v1, v2 ->`, `in range ->`, `is Type ->`, `null ->`, `else ->`. Without a subject, branches are conditions | PDF (when) / P1 (value position) / OPEN(9) (branch spellings) |
| Loops | `while cond:`; `for x in iterable:`; `break`, `continue` | PDF |
| Ranges | `a..b` excludes `b`, `a..=b` includes `b` | OPEN(7), provisional in examples |
| Functions | `fn name(a: int, b: string = "x") -> T:`; `return`; default values; named arguments at call site | PDF (fn) / P1-default (defaults, named args) |
| Lambdas and closures | Functions are values; closures capture variables. Spelling of lambdas is open; examples use `fn(x) => x * 2` | OPEN(7) |
| Errors | A function that can fail is marked `throws`; the compiler refuses an unhandled call. `throw Error("...")`, `try:` / `catch e:`, `e.message` | PDF (throws, try/catch) / OPEN(9) (typed throws, `finally`/`defer`) |
| Classes | `class Name(params):`, methods inside, `self`, closed by default, `open` to allow inheritance, `interface`, `data class` with structural `==`, printing and `copy`, `enum` | PDF (class, open, interface, data class) / OPEN(9) (`self` vs `this`, inheritance spelling, `enum`) |
| Modules | One file = one module. Everything private, `pub` exports. `use "./path"` imports your own file | PDF / OPEN(9) (alias, selective import) |
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

---

## 3. Lexical structure in detail

Tokens the lexer produces, in the order they become relevant:

| Phase | Tokens |
|---|---|
| 3 | `HEADER` (`arete 0.1`), `IDENT`, `INT`, `FLOAT`, `STRING`, `( )`, `NEWLINE`, `EOF`. Comments are skipped |
| 4 | `+ - * / // % **`, `,` |
| 5 | `let var = : < > \| ? [ ]` plus compound assignment |
| 6 | `INDENT`, `DEDENT`, `if elif else and or not`, comparisons |
| 7 | `while for in break continue fn return -> => .. ..=` |
| 11 | String parts for interpolation: `STRING_START`, `STRING_PART`, `INTERP_START`, `INTERP_END`, `STRING_END` |

Rules that hold from phase 3 on:

- Every token carries `line` and `column` (1-based). Error messages use them.
- `NEWLINE` is emitted at the end of a logical line. Inside `( [ {` no `NEWLINE`, `INDENT` or `DEDENT` is emitted.
- The indentation stack starts at 0. A line indented deeper than the top of the stack by exactly 4 emits `INDENT`; shallower lines emit one `DEDENT` per level popped. Any other depth: `error: inconsistent indentation`.
- A tab in leading whitespace: `error: tab in indentation`, hint `use 4 spaces`.
- A number is `INT` unless it contains `.` followed by a digit, or an exponent. `1..10` is `INT DOTDOT INT`.
- `>` `>` written as `>>` in a type position (`Map<string, List<int>>`) is split by the parser; there is no shift operator unless bitwise operators are added later.

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
| Package right missing | `package "http@2.1" requires [fs], but only [net] allowed` | the corrected `use` line |
| Int overflow | `` integer overflow in `a + b` `` | runtime, with position |

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

Type grammar (parsed from phase 5, checked from phase 10):

```
Type     = Union ;
Union    = Postfix { "|" Postfix } ;
Postfix  = Primary { "?" } ;
Primary  = NAME [ "<" Type { "," Type } ">" ]
         | "fn" "(" [ Type { "," Type } ] ")" "->" Type [ "throws" [ NAME ] ]
         | "(" Type ")" ;
```

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

**Phase 7 (loops, functions):** range spelling (`..`/`..=` vs `..<` vs `range()`); lambda spelling and whether a block lambda exists; capture by reference or by value; are top-level `fn` visible before their definition.

**Phase 9 (stage C syntax session):** string methods vs `len()`; string repeat `*` and substring `in`; collection literals, empty `{}`, `List` vs `MutableList`, method names (`add`/`append`); destructuring in `for`; `str()` conversion; `when` branch spellings and exhaustiveness; typed `throws`, what may be thrown, `finally`/`defer`, `rethrows`; `self` vs `this`; inheritance and override spelling; interface implementation spelling; data class method names; `enum` and `sealed`; explicit type arguments at call (`json<User>()` vs `json::<User>()`); reified vs erased generics; module alias and selective import; top-level code in modules; circular imports.

**Phase 19 (rights session):** right names; static (lexical) vs dynamic rights model; transitive rights; source of a package's requirements; standard library naming; rights of foreign code.

**Phase 26 (runtime session):** cycles under reference counting; task scheduler model; channel API and `select`; macro call marker; pointer syntax; VM architecture; dependency policy.

---

## 7. Example programs

All in `examples/pending/`. A file moves to `tests/` when its phase is reached
and it passes.

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
