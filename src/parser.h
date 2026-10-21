/* parser.h - recursive descent over the token list. One function per rule
 * of GRAMMAR.md. */
#ifndef ARETE_PARSER_H
#define ARETE_PARSER_H

#include <stdbool.h>

#include "ast.h"
#include "diag.h"
#include "token.h"

/* Builds the tree for a whole file (or, with `repl_mode`, for one REPL line
 * without a header). Returns NULL and fills `err` on the first syntax error. */
Program *parse_program(const TokenList *tokens, bool repl_mode, Diag *err);

#endif
