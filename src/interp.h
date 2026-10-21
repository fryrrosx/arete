/* interp.h - tree-walking interpreter. */
#ifndef ARETE_INTERP_H
#define ARETE_INTERP_H

#include "ast.h"

/* Runs a checked program. Returns 0, or 1 after a runtime error. */
int interp_run(const Program *prog);

#endif
