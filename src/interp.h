/* interp.h - tree-walking interpreter. An Interp lives for a whole REPL
 * session so that variables survive from one line to the next. */
#ifndef ARETE_INTERP_H
#define ARETE_INTERP_H

#include "ast.h"
#include "diag.h"

typedef struct Interp Interp;

Interp *interp_new(void);
void interp_free(Interp *in);

/* Runs a checked program. Returns 0, or 1 after a runtime error, which is
 * described in `err` with the position of the failing expression. */
int interp_run(Interp *in, const Program *prog, Diag *err);

#endif
