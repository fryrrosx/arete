/* util.h - allocation helpers. Out of memory is not a situation the
 * interpreter can do anything about, so these exit with a message instead
 * of making every caller check for NULL. */
#ifndef ARETE_UTIL_H
#define ARETE_UTIL_H

#include <stddef.h>

void *xmalloc(size_t n);
void *xcalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

#endif
