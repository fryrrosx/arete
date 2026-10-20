# Arete

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

Pre-0.1. Nothing runs yet. We are in the design phase: see `SPEC.md` for every
decision made so far and `examples/pending/` for the programs the language must
eventually run. The step-by-step plan is in `ROADMAP.md`.

## Building (from phase 3 on)

```
make build      # builds ./arete
make test       # runs tests/*.rt against tests/*.expected
make examples   # runs every example whose phase is already reached
```

Implementation language: C11, no dependencies beyond the C standard library.

## License

MIT. See `LICENSE`.
