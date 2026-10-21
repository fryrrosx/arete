# Arete — a programming language

## What this is
Arete is a new programming language I (Karim) am creating from scratch, together
with Claude Code. Syntax like Python (indentation), types like TypeScript/Kotlin,
speed goal of C/C++ (later, through LLVM). File extension: `.rt`.
Full specification: `SPEC.md`. Grammar: `GRAMMAR.md`. Plan: `ROADMAP.md`.

## Implementation
- Language: C11. Build with `make` (GNU make). Must compile with gcc, clang and MSVC.
- Current stage: phases 0-6 done, phase 7 next (loops, functions, closures,
  resolver). Pipeline: lexer -> parser -> AST -> check -> tree-walking interpreter.
  The `%` operator is Karim's exercise and is intentionally not implemented.
- No external dependencies beyond the C standard library. Exceptions are decided
  explicitly in SPEC.md (vendored single-file C libraries from phase 8 at the earliest).
- Platform-specific calls live only inside the `rt_*` runtime layer.

## Key language rules
- Line 1 of every `.rt` file is exactly `arete <major>.<minor>` (currently `arete 0.1`).
  Without it: error `missing version header`.
- The REPL starts only with a version: `arete 0.1`.
- Blocks by indentation: 4 spaces, tab in indentation is an error. No `;`.
- `let` is immutable, `var` is mutable.
- Logical operators are the words `and`, `or`, `not`.
- `/` on two ints gives a float; `//` is integer division; int overflow is a runtime error.
- Every compiler error shows file, line, column, the source line and a hint.

## How to work
- Small steps, one feature at a time, in the order of `ROADMAP.md`.
- Every feature gets a test in `tests/`: a `.rt` file plus a `.expected` file
  (exact stdout), and for errors a `.expected_error` file.
- Run `make test` after every change. Run `make examples` before closing a phase.
- Never change a decision recorded in `SPEC.md` without asking Karim first.
  If the implementation shows a decision is wrong: stop, explain, ask.
- Code comments in English. Explanations to Karim in Russian.
- Show the plan (files, functions, data structures) before writing code.
