/* rt_sys.h - the only place that talks to the operating system directly.
 * Everything platform-specific stays behind these functions (CLAUDE.md). */
#ifndef ARETE_RT_SYS_H
#define ARETE_RT_SYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* Once at start-up. On Windows this switches stdout and stderr to binary
 * mode so that `\n` is written as one byte, like everywhere else. */
void rt_sys_init(void);

/* Reads a whole file into a malloc'd, NUL-terminated buffer.
 * Returns NULL if the file cannot be opened or read. */
char *rt_read_file(const char *path, size_t *length_out);

/* Reads one line from `in` without its line end into a malloc'd buffer.
 * Returns NULL at end of input when nothing was read. */
char *rt_read_line(FILE *in);

/* True when standard input is an interactive terminal. */
bool rt_stdin_is_terminal(void);

#endif
