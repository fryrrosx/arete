/* parser.h - recursive descent for statements, a Pratt loop for
 * expressions. One function per rule of GRAMMAR.md. */
#ifndef ARETE_PARSER_H
#define ARETE_PARSER_H

#include <stdbool.h>

#include "ast.h"
#include "diag.h"
#include "token.h"

/* Builds the tree for a whole file (or, with `repl_mode`, for one REPL
 * input without a header). Returns NULL and fills `err` on the first
 * syntax error. The token list may be edited (a `>=` split into `>` `=`
 * inside a type). `src` is remembered by every function the program
 * declares, so a runtime error inside it can show the right source line;
 * the caller keeps `src` alive as long as the program. */
Program *parse_program(TokenList *tokens, bool repl_mode, const Source *src, Diag *err);

#endif
