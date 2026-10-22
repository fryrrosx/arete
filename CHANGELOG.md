# Changelog

One line per phase. Phase numbers refer to `ROADMAP.md`.

## Unreleased

- Phase 10: the first type checker, inside the resolver pass: every
  expression gets a static type (`int`, `float`, `string`, `bool`, `range`,
  `fn(...) -> T`), variables keep the type of their first value, arguments,
  returns, defaults and annotations are checked, conditions must be `bool`,
  `==` between unrelated types is refused, an `int` is accepted and
  converted where a `float` is expected, lambda parameters are inferred
  from the expected function type, annotations of later phases name their
  phase; `int ** int` is an `int` and a negative exponent is a runtime
  error; the REPL keeps types across inputs; 34 new tests. Review round:
  a named argument through a function value is type-checked and widened
  when the call runs, `print` fits any `fn(T)`, a function returning a
  value fits `fn()`, two declarations of one function in a REPL input no
  longer crash, a run-time failure undoes the input's declarations, the
  built-in `print` cannot be redeclared in the REPL, else chains and call
  chains count against the depth limit, long type names are cut with
  `...`, name lookup in scopes is hashed (100000 declarations check in
  0.2 s instead of 38 s).

- Phase 9: the stage C syntax decided and recorded (SPEC sections 2.4,
  2.5, 2.7, 5 and 6): methods only on strings and collections, `List` and
  `MutableList` (likewise Map and Set), `{}` as an empty Map, `when` branch
  bodies and blocks, untyped `throws` with `finally`, explicit `self`,
  Kotlin-style parent lists with `override`, `enum`, explicit type
  arguments `f<T>()`, `use ... as` and `use ... { names }`, destructuring
  `let (x, y) = p`; grammar sketches in GRAMMAR.md; examples 08 to 15
  rewritten; the type checker starts in phase 10.

- Phases 7 and 8 review: a function without `-> T` called through a
  variable can no longer leak a value; a lambda may call a function that
  returns nothing; reading a name whose `let` has not run is the error
  `has no value yet` (slots start unset); a failed REPL redefinition leaves
  the old function in place; ranges compare by their elements; `_` may
  repeat as a parameter name; the stack guard also covers deep expressions;
  an argument that fails to evaluate no longer releases an uninitialised
  value; clearer messages for `let print`, `print(x = 1)`, trailing commas,
  `fn class()`, a lambda without a body, a default without a value. REPL: a
  blank line after a `:` line runs the input, an error inside a block is
  reported once, stdin as a terminal with stdout redirected reads plain
  lines, NUL bytes are reported, Ctrl-D inside an input drops it, the
  history holds 1000 lines and is saved after every line; the pty test
  uses a temporary HOME.

- Phase 7: `while`, `for x in a..b` / `a..=b` with `break` and `continue`
  (a fresh loop variable per iteration), ranges as values; `fn` with typed
  parameters, default values evaluated per call, named arguments, `return`,
  recursion with a 1000-call limit and a C-stack guard; functions as values
  and one-line lambdas `fn(x) => x * 2`; closures capturing by reference;
  functions visible in their whole scope; the checker became a resolver
  that numbers local names as (hops, slot); `print` is an ordinary function
  value; examples 05, 06 and 07 promoted.

- Phase 8: the REPL takes blocks and open brackets across lines with the
  `... ` prompt, a blank line closes a block as in Python; line editing and
  history through vendored linenoise on Linux and macOS (the console's own
  editor on Windows), history file `~/.arete_history`; a pseudo-terminal
  test drives the interactive path.

- Phases 4 to 6: numbers (`int`, `float`, hex, binary, underscores,
  exponents), the Pratt expression parser with `+ - * / // **`, unary
  minus, checked 64-bit arithmetic, string `+`; `let`/`var`, assignment
  and compound assignment, the full type grammar parsed into `TypeExpr`,
  a name checker with scopes; INDENT/DEDENT, `bool`, comparisons,
  `and or not`, `if/elif/else`, `a if c else b`, block scoping;
  `--dump-tokens` and `--dump-ast`; the REPL keeps names between lines;
  examples 03 and 04 promoted. `%` is left as the phase-4 exercise.
  Review round: expression depth limit (a long chain is an error, not a
  crash), `not` refused as an operand of higher levels, number literals
  of any length, exact int/float comparison above 2^53, Python-style
  float printing and float floor division, `_` as the discard name,
  clearer messages for a missing colon, shared allocation helpers;
  135 file tests, `--dump-tokens` tested.

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
