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

/* For the REPL: what the lines typed so far look like, so that read_input
 * in main.c can decide whether the next line still belongs to them. */
typedef struct {
    bool open_bracket;      /* a `(` is still open: keep reading, blank lines included */
    bool ends_with_colon;   /* the last line ends with `:`: its body comes next */
    bool opened_block;      /* some line ended with `:` and was followed by a line end */
} InputState;

void lexer_input_state(const Source *src, InputState *out);

#endif
