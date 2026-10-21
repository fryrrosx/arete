/* lexer.h - turns source text into tokens (SPEC.md section 3). */
#ifndef ARETE_LEXER_H
#define ARETE_LEXER_H

#include <stdbool.h>

#include "diag.h"
#include "token.h"

/* Tokenizes `src` into `out`. With `expect_header` the first line must be
 * the version header (files); without it the text is a REPL line.
 * Returns false and fills `err` on the first error. `out` must be freed with
 * token_list_free in both cases. */
bool lexer_tokenize(const Source *src, bool expect_header,
                    TokenList *out, Diag *err);

/* For the REPL: is this input incomplete, so that the next line still
 * belongs to it? True when a bracket is still open or the last logical
 * line ends with `:` (a block opener). `in_block` reports whether the
 * input has opened a block at all, so the REPL knows to wait for the
 * blank line that ends it. */
bool lexer_input_incomplete(const Source *src, bool *in_block);

#endif
