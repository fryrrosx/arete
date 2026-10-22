# HANDOFF — how to continue Arete in a new session

Read this after `CLAUDE.md` and before touching anything. It records what the
previous sessions learned: the working rhythm with Karim, the definition of
done for a phase, the shape of the code, and the mistakes already made once.
Update it at the end of every phase.

## 1. Where things stand (2026-10-22)

- Phases 0–9 are done and on `main`. Phase 10 (the first type checker, path
  B) is next and was asked for by Karim on 2026-10-22 together with 7 and 9.
  After it, nothing is pre-authorised: never start a phase, a sub-phase, or
  a "quick improvement" he did not ask for.
- CI (`.github/workflows/ci.yml`) is green on Ubuntu, macOS and MSVC. Every
  push to `main` runs it; keep all three jobs green.
- Karim's open exercise: the `%` and `%=` operators (ROADMAP ritual, step 8).
  Do not implement them, even if asked to "finish phase 4";
  `tests/percent_not_yet.rt` pins the current message. After he does it,
  `examples/pending/02_arithmetic.rt` is promoted with `sh tests/promote.sh`.
- Git tags (`phase-01-spec`, `phase-02-grammar`, `phase-03-hello`,
  `phase-04-06-expressions`, `phase-08-repl`) are local only: pushing tags is
  refused by the proxy (403). None are on GitHub.
- The repository is public; making it private is Karim's manual step.
- Karim's own first sketch is `examples/drafts/karim_01.rt`, reviewed in
  `examples/drafts/karim_01_review.md`.
- Review notes from the phase 4–6 round were kept outside the repo; what
  mattered was fixed in commit `5567975`.
- Phases 4–6 count as done although the `%` exercise is still open: Karim
  chose to go on to phase 8 first. The ROADMAP status line says so.

### Open items (do these before or at the start of the next phase)

- Karim's `%` exercise (see above) and the phase 7 exercise `repeat n:`
  proposed in the phase 7 walkthrough.
- The phase 7 and 8 review rounds ran with three or four finders each; the
  verify stage was cut by a usage limit, so the findings were judged by
  hand and fixed in the commit "Phase 7 and 8 review". Lenses worth
  repeating in later phases: a reviewer that drives the REPL through a
  pseudo-terminal found most of the phase 8 defects.

## 2. Who Karim is and how he works

- Karim is learning language implementation while building Arete; the project
  doubles as his course, in the order of Crafting Interpreters. Explain things
  to him in Russian, in plain language, and connect them to what he already
  saw in earlier phases. Code, comments, commit messages and the English docs
  stay in English; `ROADMAP.md` is in Russian on purpose.
- He gives phase-level instructions ("do phase 8 only", "давай начинай фазу
  4-5-6"). Do exactly that scope. "Only" means only.
- He answers decision questions in batches. Ask with concrete options, the
  consequences of each, and a recommended default. At most about three batches
  per phase, and never a question `SPEC.md` already answers.
- He works on macOS, follows the session in the Claude app, and often cannot
  answer mid-task: do everything that does not depend on an answer first, then
  ask, then continue.
- When he asks a question ("какая самая сложная фаза?", "какие ошибки были?"),
  answer it. Do not start work on the strength of a question.
- At the end of a phase he expects a Russian walkthrough: how it works, why
  it is built that way, what went wrong on the way, and what his exercise is.
  The exercise for the next phase is proposed in that walkthrough.
- Ritual step 2 ("Karim writes the two tests before the code") was not
  practised in phases 3–8: Claude wrote the tests, Karim decided syntax and
  keeps the exercises. Ask him once whether he wants step 2 back.
- How his exercise lands is not decided yet. Default: he commits it himself or
  sends the diff; Claude reviews it; the `pending` notes in `GRAMMAR.md`
  (lines for `%` and `%=`), the SPEC §2.3 row and `tests/percent_not_yet.*`
  are then his to update, with help on request.

## 3. The ritual for one phase, as practised

`ROADMAP.md`, section "Ритуал одной фазы", is the source; this is how it was
done in phases 3–8.

1. Read the phase section in `ROADMAP.md`, the rows for its constructs in
   `SPEC.md` (§2 decisions, §4 error texts, §5 syntax by construct), and
   `GRAMMAR.md`.
2. "Решаем вместе" items: one or two question batches to Karim. Record each
   answer in `SPEC.md` with its status (`P<N>` decided in phase N,
   `P<N>-default` a default taken in phase N, `OPEN(n)` still open).
3. Show the plan in chat before any code: files, functions, data structures,
   the tests to add, the exact error texts. Then build.
4. One feature at a time; `make test` after each. Every feature gets a
   positive test (`tests/x.rt` + `tests/x.expected`, exact stdout) and a
   negative one (`tests/y.rt` + `tests/y.expected_error`, one line that
   stderr must contain, normally the error's summary line). `tests/run.sh`
   compares `.expected`, `.expected_ast` and `.expected_tokens` byte for byte
   and checks `.expected_error` as a substring; the exact shape of the error
   block is pinned by `tests/cli.sh`. Write an expected file from real output
   only after reading that output and agreeing it is right.
5. `GRAMMAR.md` against the parser: every EBNF rule is one parser function;
   check first-token sets and left recursion for the new rules.
6. Docs in the same commit: `SPEC.md` rows and statuses, `GRAMMAR.md`,
   the status line and phase note in `ROADMAP.md`, the "Unreleased" entry in
   `CHANGELOG.md`, the "Current stage" line in `CLAUDE.md`, the "Status"
   section of `README.md`, and this file.
7. Definition of done, all of it, before the commit (commands in §4):
   gcc with `-Werror`, clang with `-Werror`, gcc with ASan/UBSan and leak
   detection, `make test`, `make examples`, and on POSIX the pty REPL test
   (`tests/cli.sh` runs it).
8. Review round: three or four subagents (the Agent tool, or the Workflow
   tool when it is enabled), each with one lens and no chat history: C code
   and memory (reads the diff, runs the ASan build); tests and edge cases
   (invents inputs, runs the binary, compares with SPEC); grammar against the
   parser (one EBNF rule = one function, first-token sets, left recursion);
   spec and docs drift (SPEC rows, ROADMAP status, CHANGELOG, README). Each
   returns numbered findings with severity, file:line, the input tried, and a
   suggested fix. Verify every finding on the binary before acting. Fix the
   real ones in a follow-up commit "Phase N review: …"; put the deferred ones
   into "Open items" above. A finding that questions a `SPEC.md` decision goes
   to Karim, not into the code.
9. Commit, push `main`, check CI on all three jobs, local tag
   `phase-NN-name`, then the Russian walkthrough. One commit "Phase N: …" per
   phase was the norm; sub-phases (7a–7d) may be separate commits on `main`
   if each leaves CI green. Pushing works with `git push -u origin main`
   through the environment's git proxy (retry on network errors); `gh` is
   read-only here, so CI status comes from the GitHub tools of the session or
   `gh run list -R fryrrosx/arete`.

## 4. Commands

```
make clean && make build CFLAGS="-Werror -g" && make test && make examples
make clean && make build CC=clang CFLAGS="-Werror -g" && make test
make clean && make build CC=gcc CFLAGS="-Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 make test && make examples
make clean && make build          # leave a normal build behind
```

- Run the REPL and its tests with `HOME` set to a scratch directory: the REPL
  writes `$HOME/.arete_history`.
- Debug output: `./arete run --dump-tokens file.rt` and
  `./arete run --dump-ast file.rt` (the flag goes before the file).
  `tests/run.sh` understands `.expected`, `.expected_error`, `.expected_ast`
  and `.expected_tokens`.
- `sh tests/promote.sh examples/pending/NN_name.rt` moves an example whose
  phase is reached into `examples/` and writes its expected file from the
  `# expected output:` block. Run the example first and read the output.
- Commit messages: title "Phase N: …" or "Phase N review: …", a body that
  says what changed and why, then the attribution trailers the harness asks
  for. Apart from those trailers, no model names in the repository: not in
  files, code comments, PR titles or bodies.
- CI on Windows compiles `src\*.c` with MSVC (`/W4`, no `/WX`) and runs the
  shell tests under Git Bash. A POSIX-only source file must be guarded the way
  `src/linenoise.c` is, so that MSVC sees a non-empty, compilable unit.

## 5. Code map (phase 8 state)

| File | Role |
|---|---|
| `src/main.c` | Command line: `arete run file.rt`, `arete 0.1` (REPL), `--dump-tokens`, `--dump-ast`; `read_input` gathers one complete REPL input |
| `src/version.c/.h` | The `arete <major>.<minor>` header and the REPL version argument |
| `src/diag.c/.h` | `Source`, `Diag`; error printing in the SPEC §4 format with a character-aware caret |
| `src/token.h`, `src/lexer.c/.h` | Tokens, indentation stack (INDENT/DEDENT), numbers, strings, the REPL-only rules, `lexer_input_incomplete` |
| `src/ast.c/.h` | `Expr`, `Stmt`, `TypeExpr`, allocation helpers, freeing |
| `src/parser.c/.h` | Pratt expression parser (`INFIX` table, levels OR=3 … POWER=12), statements, blocks, the type grammar |
| `src/check.c/.h` | Name checker with scopes (`let` vs `var`, use before declaration, redeclaration); same shape as `Env` on purpose |
| `src/env.c/.h` | Run-time scope chain |
| `src/interp.c/.h` | Tree-walking interpreter, checked 64-bit arithmetic, short-circuit `and`/`or` |
| `src/arete_rt.h`, `src/rt_value.c` | Tagged-union `Value`, refcounted `ObjString`, Python-style float printing |
| `src/rt_sys.c/.h` | The only place with platform calls: files, lines, terminal test, linenoise prompt and history on POSIX, console on Windows |
| `src/dump.c/.h` | `--dump-tokens` and `--dump-ast` printers |
| `src/util.c/.h` | Small shared helpers |
| `src/linenoise.c/.h` | Vendored, BSD-2, POSIX only, not edited beyond the `_WIN32` guard; pristine sha256 in its header |
| `tests/run.sh`, `tests/cli.sh`, `tests/repl_pty.py`, `tests/promote.sh` | File tests, command-line and REPL tests, the pseudo-terminal test, example promotion |

Pipeline: lexer → parser → AST → check → interpreter. Expression depth is
limited (SPEC §3) so deep input is an error, not a crash.

## 6. Mistakes already made; do not repeat them

- `pkill -f '<pattern>'` killed the session's own shell, because the pattern
  matched the shell's command line. Kill stray processes by pid.
- An interrupted sanitizer build left `*.o` files compiled with ASan; the next
  plain `make build` linked them and failed with undefined `__asan_*` symbols.
  `make clean` whenever `CC` or `CFLAGS` change.
- The pty test hung: linenoise asks the terminal for the cursor position when
  the window is 0×0 and waits for an answer forever. The test sets a 24×80
  window, uses `select()` with timeouts, and waits for the prompt that comes
  after the previous output (not any earlier prompt), because linenoise
  discards typed-ahead input when it switches the terminal mode.
- Since phase 8 an unclosed `(` means "keep typing", so a REPL test of
  "error, then the session continues" needs an error that is complete on one
  line, such as `print(1 // 0)`.
- The MSVC job failed twice on the `/dev/full` write-failure check: Git Bash
  reports `/dev/full` as writable. That check is skipped on Windows via
  `uname`. Always think about the Windows job before pushing.
- `.gitattributes` normalises to LF; test files that need CRLF or a BOM are
  marked `-text`. Makefile recipes need real tabs.
- `1e3` printed as `1e+03`; the fix was a Python-repr float printer in
  `rt_value.c`. Any new numeric output goes through it.
- `if true` without a colon was misreported by the lexer; the decision about
  INDENT after a missing colon belongs to the parser's error path.
- Error texts are decisions (SPEC §4 table). Do not reword an existing message
  without updating `SPEC.md` and the `.expected_error` files together.
- Generated tests were once wrong because the expected file was produced from
  a buggy binary. Read the output before promoting it to `.expected`.
- If you use the Workflow tool, keep backticks out of prompts written inside
  template literals; one script failed to parse that way.
- A sentinel that is also a value is a bug waiting to happen: `null` was
  used to mean "argument not given" and leaked out of void functions. Slots
  now start as `VAL_UNSET`, which no program can observe.
- A test that touches dotfiles in `$HOME` touches the developer's real
  files when `make test` runs on their machine. The pty test makes its own
  temporary HOME; keep it that way for anything that reads or writes
  outside the repository.
- The phase 8 review workflow's verify agents all failed on a usage limit:
  when a workflow result says "refuted" with reason "no verdict", nothing
  was refuted. Read the journal.
- Never change a `SPEC.md` decision silently; never implement `%`; never edit
  the vendored linenoise body.

## 7. Notes for phase 10 and after

- Phase 9 recorded the stage C syntax in `SPEC.md` §6 "Phase 9" (twelve
  decisions by Karim, the rest P9-default) and in grammar sketches at the end
  of `GRAMMAR.md`; `examples/pending/08`–`15` use that syntax and are the
  acceptance tests of phases 11–18.
- Phase 10 (decided: path B): a `types.c` pass after `check.c`, bidirectional
  (annotations flow down, literals and operators flow up), types `int`,
  `float`, `string`, `bool`, `fn(...) -> T`, `range`, `let` inference,
  function signatures and calls, `if`/`while` conditions, operators; the
  REPL keeps a type environment per session like the checker's scope. The
  "Готово" criteria: `var x = 5` then `x = "a"` is `expected int, found
  string`; a string passed to `fn f(a: int)` is an error with a hint. Every
  later phase adds its feature's types to that pass.

## 8. How phase 7 is built (read before touching functions)

- `check.c` is the resolver: every scope it opens is one `ObjEnv` at run
  time, in the same order (block, `for` body with the loop variable in slot 0,
  function body with parameters first, lambda). If you add a construct that
  opens a scope, add it to both or (hops, slot) go wrong.
- Functions are hoisted per statement list: `declare_functions` (checker) and
  `define_functions` (interpreter) run before the list's statements.
- Closures make reference cycles; `rt_shutdown` sweeps every `ObjEnv` and
  `ObjFunction` at exit. Mid-run leaks of cyclic closures are expected until
  phase 29. ASan runs stay clean because of the sweep.
- `call_function` in `interp.c` and `check_arguments` in `check.c` apply the
  same argument rules with the same messages; change both together.
- A REPL input's program and text live until the session ends (`Session` in
  `main.c`), because function values point into the tree; `Diag.source` lets
  an error inside such a function show the right input's line.

- `check.c` and `env.c` are the same shape on purpose; the resolver of 7d
  replaces the checker's scope with slot indices and makes `Env` slot-based.
- Candidates for promotion at the end: `examples/pending/05_loops.rt`,
  `06_functions.rt`, `07_closures.rt`. They were written in phase 1 and may
  assume syntax that the phase 7 decisions change; fix the example with Karim,
  never bend the implementation to a stale example.
- Reading: Crafting Interpreters chapters 10–11.
