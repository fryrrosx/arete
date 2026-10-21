/* check.h - checks after parsing and before running. In phase 3 this only
 * knows one built-in function. It grows into the resolver (phase 7) and the
 * type checker (phase 10). */
#ifndef ARETE_CHECK_H
#define ARETE_CHECK_H

#include <stdbool.h>

#include "ast.h"
#include "diag.h"

bool check_program(const Program *prog, Diag *err);

#endif
