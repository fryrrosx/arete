# Arete

[![ci](https://github.com/fryrrosx/arete/actions/workflows/ci.yml/badge.svg)](https://github.com/fryrrosx/arete/actions/workflows/ci.yml)

**All the libraries in the world. Safely. Forever.**

Arete is a new programming language, built from scratch. Syntax as simple as
Python, types like TypeScript and Kotlin, speed of C/C++ as the goal.
Files use the `.rt` extension.

```
arete 0.1

let name = "World"
print("Hello, " + name + "!")
```

Three ideas drive the design:

- **All the libraries in the world.** Import code from C, Python and JavaScript directly.
- **Safely.** Every package gets only the rights you grant it: network, files, environment, processes.
- **Forever.** The language version and every dependency version are pinned in the file itself.
  The program runs the same way years later.

## Status

Pre-0.1, phases 0–8 of `ROADMAP.md`: numbers, strings, `let`/`var`,
`if/elif/else`, `while`/`for` over ranges, functions with defaults and named
arguments, lambdas and closures, and a REPL with multi-line input and
history, and a static type checker (`int`, `float`, `string`, `bool`,
`range`, function types; inference for `let` and for lambda parameters). The
syntax of stage C (strings, collections, `when`, errors, classes, null
safety, generics, modules) is decided in `SPEC.md`; those constructs come
next, one phase each. `SPEC.md` holds every decision made,
`examples/pending/` the programs the language must eventually run.

## Building

```
make build      # builds ./arete
make test       # runs tests/*.rt against tests/*.expected
make examples   # runs every example whose phase is already reached
```

Implementation language: C11, no dependencies beyond the C standard library
(the REPL's line editor is the vendored `linenoise`, BSD-2, POSIX only).

## License

MIT. See `LICENSE`. The REPL line editor `src/linenoise.c` and `src/linenoise.h`
is vendored from antirez/linenoise under the BSD-2 license; see
`LICENSE-linenoise`.
