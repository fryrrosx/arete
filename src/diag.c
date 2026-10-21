#include "diag.h"

#include <stdio.h>
#include <string.h>

static void copy_field(char *dst, size_t cap, const char *src) {
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, cap, "%s", src);
}

void diag_set(Diag *d, int line, int column,
              const char *summary, const char *label, const char *help) {
    d->is_set = true;
    d->line = line < 1 ? 1 : line;
    d->column = column < 1 ? 1 : column;
    copy_field(d->summary, sizeof d->summary, summary);
    copy_field(d->label, sizeof d->label, label);
    copy_field(d->help, sizeof d->help, help);
}

/* Returns the start of line `line` (1-based) and its length without the line
 * end. A line past the end of the text is empty. */
static const char *find_line(const Source *src, int line, size_t *len_out) {
    const char *p = src->text;
    const char *end = src->text + src->length;
    int current = 1;
    while (current < line && p < end) {
        if (*p == '\n') current++;
        p++;
    }
    const char *start = p;
    while (p < end && *p != '\n') p++;
    size_t len = (size_t)(p - start);
    if (len > 0 && start[len - 1] == '\r') len--;
    *len_out = len;
    return start;
}

static int digit_count(int n) {
    int digits = 1;
    while (n >= 10) {
        n /= 10;
        digits++;
    }
    return digits;
}

static bool is_continuation_byte(char c) {
    return ((unsigned char)c & 0xC0) == 0x80;
}

/* Columns are counted in bytes (that is what editors expect in the `-->`
 * line), but the caret must land under the right character on screen, so
 * the padding is computed per character: a tab stays a tab and the extra
 * bytes of a UTF-8 character take no space. */
static void print_caret_padding(const char *text, size_t from, size_t to) {
    for (size_t i = from; i < to; i++) {
        if (text[i] == '\t') {
            fputc('\t', stderr);
        } else if (!is_continuation_byte(text[i])) {
            fputc(' ', stderr);
        }
    }
}

#define WINDOW 120   /* longest source line shown in full */

void diag_print(const Source *src, const Diag *d) {
    size_t len = 0;
    const char *text = find_line(src, d->line, &len);
    size_t column = (size_t)d->column - 1;
    if (column > len) column = len;

    /* Very long lines are cut to a window around the error. */
    size_t from = 0, to = len;
    if (len > WINDOW) {
        from = column > WINDOW / 2 ? column - WINDOW / 2 : 0;
        to = from + WINDOW < len ? from + WINDOW : len;
        while (from > 0 && is_continuation_byte(text[from])) from--;
        while (to < len && is_continuation_byte(text[to])) to++;
    }
    const char *pre = from > 0 ? "..." : "";
    const char *post = to < len ? "..." : "";

    int w = digit_count(d->line);
    fprintf(stderr, "error: %s\n", d->summary);
    fprintf(stderr, "  --> %s:%d:%d\n", src->file, d->line, d->column);
    fprintf(stderr, " %*s |\n", w, "");
    fprintf(stderr, " %*d | %s%.*s%s\n", w, d->line, pre, (int)(to - from), text + from, post);
    fprintf(stderr, " %*s | %*s", w, "", (int)strlen(pre), "");
    print_caret_padding(text, from, column);
    fputc('^', stderr);
    if (d->label[0] != '\0') fprintf(stderr, " %s", d->label);
    fputc('\n', stderr);
    if (d->help[0] != '\0') fprintf(stderr, " %*s = help: %s\n", w, "", d->help);
}
