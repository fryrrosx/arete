/* dump.h - `--dump-tokens` and `--dump-ast`: the two windows into the
 * front end, for understanding and for tests. */
#ifndef ARETE_DUMP_H
#define ARETE_DUMP_H

#include <stdio.h>

#include "ast.h"
#include "token.h"

/* One token per line: "2:1 IDENT print". */
void dump_tokens(const TokenList *tokens, FILE *out);

/* One statement per line as S-expressions: "(print (+ 1 (* 2 3)))". */
void dump_program(const Program *prog, FILE *out);

#endif
