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
char *rt_read_line(FILE *in, size_t *length_out);

/* Reads one line of interactive input, showing `prompt` first. This is
 * where line editing and history live when the terminal supports them.
 * Only called when both stdin and stdout are terminals. Returns NULL at
 * end of input (after printing the line end the user did not get), or NULL
 * with *cancelled set when the user pressed Ctrl-C. */
char *rt_read_line_prompt(const char *prompt, bool *cancelled);

/* History of interactive input: loaded from ~/.arete_history at the start
 * of a session, extended and saved line by line (so nothing is lost when
 * the session ends abruptly), saved once more at the end. */
void rt_history_load(void);
void rt_history_add(const char *line);
void rt_history_save(void);

/* True when standard input / output is an interactive terminal. */
bool rt_stdin_is_terminal(void);
bool rt_stdout_is_terminal(void);

/* Bytes of stack the main thread has, as far as the platform tells; 8 MB
 * when it does not. The interpreter keeps its walk inside this. */
size_t rt_stack_limit(void);

#endif
