#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif

#include "rt_sys.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#define ARETE_ISATTY(fd) _isatty(fd)
#define ARETE_FILENO(f) _fileno(f)
#else
#include <errno.h>
#include <unistd.h>
#include "linenoise.h"
#define ARETE_ISATTY(fd) isatty(fd)
#define ARETE_FILENO(f) fileno(f)
#endif

#define HISTORY_FILE ".arete_history"
#define HISTORY_LINES 1000

void rt_sys_init(void) {
#if defined(_WIN32)
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
}

char *rt_read_file(const char *path, size_t *length_out) {
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;

    size_t capacity = 4096;
    size_t length = 0;
    char *buffer = malloc(capacity);
    if (buffer == NULL) {
        fclose(f);
        return NULL;
    }
    for (;;) {
        if (length + 1 >= capacity) {
            capacity *= 2;
            char *grown = realloc(buffer, capacity);
            if (grown == NULL) {
                free(buffer);
                fclose(f);
                return NULL;
            }
            buffer = grown;
        }
        size_t n = fread(buffer + length, 1, capacity - length - 1, f);
        length += n;
        if (n == 0) break;
    }
    bool failed = ferror(f) != 0;
    fclose(f);
    if (failed) {
        free(buffer);
        return NULL;
    }
    buffer[length] = '\0';
    *length_out = length;
    return buffer;
}

char *rt_read_line(FILE *in) {
    size_t capacity = 128;
    size_t length = 0;
    char *buffer = malloc(capacity);
    if (buffer == NULL) return NULL;
    for (;;) {
        int c = fgetc(in);
        if (c == EOF) {
            if (length == 0) {
                free(buffer);
                return NULL;
            }
            break;
        }
        if (c == '\n') break;
        if (length + 1 >= capacity) {
            capacity *= 2;
            char *grown = realloc(buffer, capacity);
            if (grown == NULL) {
                free(buffer);
                return NULL;
            }
            buffer = grown;
        }
        buffer[length++] = (char)c;
    }
    if (length > 0 && buffer[length - 1] == '\r') length--;
    buffer[length] = '\0';
    return buffer;
}

#if defined(_WIN32)

/* The Windows console edits lines and keeps a history on its own. */
char *rt_read_line_prompt(const char *prompt, bool *cancelled) {
    *cancelled = false;
    fputs(prompt, stdout);
    fflush(stdout);
    return rt_read_line(stdin);
}

void rt_history_load(void) {}
void rt_history_add(const char *line) { (void)line; }
void rt_history_save(void) {}

#else

/* Path of the history file in the home directory, or NULL. */
static char *history_path(void) {
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0') return NULL;
    size_t n = strlen(home) + 1 + strlen(HISTORY_FILE) + 1;
    char *path = malloc(n);
    if (path == NULL) return NULL;
    snprintf(path, n, "%s/%s", home, HISTORY_FILE);
    return path;
}

char *rt_read_line_prompt(const char *prompt, bool *cancelled) {
    *cancelled = false;
    errno = 0;
    char *line = linenoise(prompt);
    if (line == NULL && errno == EAGAIN) *cancelled = true;   /* Ctrl-C */
    return line;                                              /* malloc'd, free() it */
}

void rt_history_load(void) {
    linenoiseHistorySetMaxLen(HISTORY_LINES);
    char *path = history_path();
    if (path != NULL) linenoiseHistoryLoad(path);
    free(path);
}

void rt_history_add(const char *line) {
    linenoiseHistoryAdd(line);
}

void rt_history_save(void) {
    char *path = history_path();
    if (path != NULL) linenoiseHistorySave(path);
    free(path);
}

#endif

bool rt_stdin_is_terminal(void) {
    return ARETE_ISATTY(ARETE_FILENO(stdin)) != 0;
}
