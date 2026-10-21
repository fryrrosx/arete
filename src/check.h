/* check.h - checks after parsing and before running: names are declared
 * before use, `let` is never assigned, a name is declared once per scope,
 * `print` is called correctly. This grows into the resolver (phase 7) and
 * the type checker (phase 10). A Checker lives for a whole REPL session so
 * that names survive from one line to the next. */
#ifndef ARETE_CHECK_H
#define ARETE_CHECK_H

#include <stdbool.h>

#include "ast.h"
#include "diag.h"

typedef struct Checker Checker;

Checker *checker_new(bool repl_mode);
void checker_free(Checker *c);

bool check_program(Checker *c, const Program *prog, Diag *err);

#endif
