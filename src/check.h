/* check.h - the resolver: checks after parsing and before running that
 * names are declared before use, `let` is never assigned, a name is
 * declared once per scope, `return`/`break`/`continue` are placed right and
 * known functions are called correctly; numbers local names as
 * (hops, slot). The type checker (phase 10) grows out of it. A Checker
 * lives for a whole REPL session so that names survive between inputs. */
#ifndef ARETE_CHECK_H
#define ARETE_CHECK_H

#include <stdbool.h>

#include "ast.h"
#include "diag.h"

typedef struct Checker Checker;

Checker *checker_new(bool repl_mode);
void checker_free(Checker *c);

/* Checks and resolves the program, writing (hops, slot) into its names. */
bool check_program(Checker *c, Program *prog, Diag *err);

#endif
