#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void out_of_memory(void) {
    fputs("fatal: out of memory\n", stderr);
    exit(2);
}

void *xmalloc(size_t n) {
    void *p = malloc(n == 0 ? 1 : n);
    if (p == NULL) out_of_memory();
    return p;
}

void *xcalloc(size_t n) {
    void *p = calloc(1, n == 0 ? 1 : n);
    if (p == NULL) out_of_memory();
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n == 0 ? 1 : n);
    if (q == NULL) out_of_memory();
    return q;
}

char *xstrndup(const char *s, size_t n) {
    char *c = xmalloc(n + 1);
    memcpy(c, s, n);
    c[n] = '\0';
    return c;
}

char *xstrdup(const char *s) {
    return xstrndup(s, strlen(s));
}
