# Changelog

One line per phase. Phase numbers refer to `ROADMAP.md`.

## Unreleased

- Phases 4 to 6: numbers (`int`, `float`, hex, binary, underscores,
  exponents), the Pratt expression parser with `+ - * / // **`, unary
  minus, checked 64-bit arithmetic, string `+`; `let`/`var`, assignment
  and compound assignment, the full type grammar parsed into `TypeExpr`,
  a name checker with scopes; INDENT/DEDENT, `bool`, comparisons,
  `and or not`, `if/elif/else`, `a if c else b`, block scoping;
  `--dump-tokens` and `--dump-ast`; the REPL keeps names between lines;
  examples 03 and 04 promoted; 100 file tests. `%` is left as the
  phase-4 exercise.

- Phase 3: `arete run` and the one-line REPL; version header check; lexer
  with positions, comments, BOM/CRLF, string escapes; parser, checker and
  tree-walking interpreter for `print("...")`; reference-counted `Value`
  in `arete_rt.h`; 29 file tests and a CLI test script; `tests/promote.sh`;
  example 01 promoted; CI on Linux and macOS, MSVC build on Windows.
  Review round: version spelling compared as text (no leading zeros),
  character-aware caret, byte-exact test comparison, REPL exit code,
  header-on-wrong-line message, extra-argument errors, 17 more tests.
- Phase 2: `GRAMMAR.md` with the EBNF for phases 3 to 5, first-token and
  left-recursion checks, associativity derivations.

- Phase 0: repository, license (MIT), build and test skeleton, CLAUDE.md.
- Phase 1: SPEC.md v1 with all decisions of the design discussion plus the
  eleven decisions of 2026-10-20; 20 example programs in `examples/pending/`;
  four independent reviews applied (68 items); GRAMMAR.md parsing notes.
  Defaults left for Karim to confirm: name collision with jsiek/arete,
  contextual modifier keywords, `elif`, line comments only.
