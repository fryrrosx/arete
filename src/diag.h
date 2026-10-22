/* diag.h - error reporting in the one shape every Arete error has.
 * See SPEC.md section 4. */
#ifndef ARETE_DIAG_H
#define ARETE_DIAG_H

#include <stdbool.h>
#include <stddef.h>

/* A source text plus the name shown in messages. `text` is NUL-terminated. */
typedef struct {
    const char *file;
    const char *text;
    size_t length;
} Source;

/* One error. `summary` is the first line, `label` sits under the caret,
 * `help` is the "= help:" line. Empty strings are omitted when printing. */
typedef struct {
    bool is_set;
    int line;
    int column;
    const Source *source;   /* where line/column point; NULL means the text being run */
    char summary[480];      /* room for "expected T, found U" with two long function types */
    char label[480];
    char help[320];
} Diag;

void diag_set(Diag *d, int line, int column,
              const char *summary, const char *label, const char *help);

/* Prints the error to stderr:
 *   error: <summary>
 *     --> <file>:<line>:<column>
 *      |
 *    7 | <source line>
 *      |     ^ <label>
 *      = help: <help>
 */
void diag_print(const Source *src, const Diag *d);

#endif
