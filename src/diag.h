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
    char summary[200];
    char label[200];
    char help[240];
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
