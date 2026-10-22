/* main.c - the command line:
 *   arete run <file.rt>                 run a file
 *   arete run --dump-tokens <file.rt>   show the tokens instead
 *   arete run --dump-ast <file.rt>      show the syntax tree instead
 *   arete <version>                     start the REPL for that language version
 * Errors go to stderr; the exit code is 1 on any error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "diag.h"
#include "dump.h"
#include "interp.h"
#include "lexer.h"
#include "parser.h"
#include "rt_sys.h"
#include "version.h"

typedef enum { MODE_RUN, MODE_DUMP_TOKENS, MODE_DUMP_AST } Mode;

/* The state that outlives one source text: in the REPL, names and values
 * must survive from one input to the next. Programs and their source texts
 * are kept until the session ends, because a function value points at the
 * tree it was declared in. */
typedef struct {
    Checker *checker;
    Interp *interp;
    Program **programs;
    Source **sources;
    size_t count;
    size_t capacity;
} Session;

static void session_init(Session *s, bool repl_mode) {
    s->checker = checker_new(repl_mode);
    s->interp = interp_new();
    s->programs = NULL;
    s->sources = NULL;
    s->count = s->capacity = 0;
}

/* Takes ownership of `src` (and its text) and, later, of the program it
 * produces. */
static void session_keep(Session *s, Source *src, Program *prog) {
    if (s->count == s->capacity) {
        s->capacity = s->capacity == 0 ? 16 : s->capacity * 2;
        s->programs = realloc(s->programs, s->capacity * sizeof(Program *));
        s->sources = realloc(s->sources, s->capacity * sizeof(Source *));
        if (s->programs == NULL || s->sources == NULL) {
            fputs("fatal: out of memory\n", stderr);
            exit(2);
        }
    }
    s->programs[s->count] = prog;
    s->sources[s->count] = src;
    s->count++;
}

static void session_free(Session *s) {
    interp_free(s->interp);            /* releases every value first */
    checker_free(s->checker);
    for (size_t i = 0; i < s->count; i++) {
        program_free(s->programs[i]);
        free((char *)s->sources[i]->text);
        free(s->sources[i]);
    }
    free(s->programs);
    free(s->sources);
}

static void print_usage(FILE *out) {
    fprintf(out, "  usage: arete <version>\n");
    fprintf(out, "         arete run [--dump-tokens | --dump-ast] <file.rt>\n");
    fprintf(out, "  installed: ");
    version_print_installed(out);
    fputc('\n', out);
}

/* Lexes, parses, checks and runs (or dumps) one source text. The session
 * takes ownership of `src`. */
static int run_source(Session *s, Source *src, bool repl_mode, Mode mode) {
    Diag err = {0};
    TokenList tokens = {0};
    Program *prog = NULL;
    int code = 1;

    if (!lexer_tokenize(src, !repl_mode, &tokens, &err)) goto done;
    if (mode == MODE_DUMP_TOKENS) {
        dump_tokens(&tokens, stdout);
        code = 0;
        goto done;
    }
    prog = parse_program(&tokens, repl_mode, src, &err);
    if (prog == NULL) goto done;
    if (mode == MODE_DUMP_AST) {
        dump_program(prog, stdout);
        code = 0;
    } else if (check_program(s->checker, prog, &err)) {
        code = interp_run(s->interp, prog, &err);
        if (code != 0 && repl_mode) checker_undo_last(s->checker);
    }

done:
    if (err.is_set) diag_print(src, &err);
    token_list_free(&tokens);
    session_keep(s, src, prog);
    return code;
}

static Source *source_new(const char *file, char *text, size_t length) {
    Source *src = malloc(sizeof *src);
    if (src == NULL) {
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    src->file = file;
    src->text = text;
    src->length = length;
    return src;
}

static int cmd_run(const char *path, Mode mode) {
    size_t length = 0;
    char *text = rt_read_file(path, &length);
    if (text == NULL) {
        fprintf(stderr, "error: cannot open file `%s`\n", path);
        return 1;
    }
    Session s;
    session_init(&s, false);
    int code = run_source(&s, source_new(path, text, length), false, mode);
    session_free(&s);
    return code;
}

/* Appends `n` bytes of `line` and a newline to the malloc'd buffer. */
static char *buffer_append(char *buffer, size_t *length, const char *line, size_t n) {
    buffer = realloc(buffer, *length + n + 2);
    if (buffer == NULL) {
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    memcpy(buffer + *length, line, n);
    *length += n;
    buffer[(*length)++] = '\n';
    buffer[*length] = '\0';
    return buffer;
}

/* The REPL buffer ends with a line end after its text; a NUL byte typed
 * into it must reach the lexer, so the length is not strlen. The buffer
 * holds "\n"-terminated lines, and buffer_append keeps a NUL after the
 * last one, so the length is found by scanning for that final "\n\0". */
static size_t input_length(const char *buffer) {
    size_t n = 0;
    for (;;) {
        size_t chunk = strlen(buffer + n);
        n += chunk;
        if (chunk > 0 && buffer[n - 1] == '\n') return n;
        if (buffer[n] == '\0' && (n == 0 || buffer[n - 1] == '\n')) return n;
        n++;                                     /* a NUL inside the text: step over it */
    }
}

static bool line_is_blank(const char *line) {
    for (; *line != '\0'; line++) {
        if (*line != ' ' && *line != '\t' && *line != '\r') return false;
    }
    return true;
}

/* Reads one complete input: a single statement, or a block and the lines
 * that follow it until a blank line, as in Python. The continuation prompt
 * `... ` shows while more lines are needed. A blank line always ends the
 * input unless a bracket is open, so `if true:` followed by an empty line
 * runs (and reports the missing body) instead of waiting forever. Once a
 * block has opened, its lines are collected without looking at them: a
 * stray error inside is reported once, when the whole block runs. Returns
 * NULL at end of input when nothing was read, or when Ctrl-D cut an input
 * short (that input is dropped, as with Ctrl-C). */
static char *read_input(bool interactive) {
    char *buffer = NULL;
    size_t length = 0;
    bool in_block = false;                       /* a `:` line and its line end were seen */
    for (;;) {
        const char *prompt = buffer == NULL ? ">>> " : "... ";
        bool cancelled = false;
        size_t line_length = 0;
        char *line = interactive ? rt_read_line_prompt(prompt, &cancelled)
                                 : rt_read_line(stdin, &line_length);
        if (line != NULL && interactive) line_length = strlen(line);
        if (line == NULL && cancelled) {         /* Ctrl-C: drop this input, start over */
            free(buffer);
            buffer = NULL;
            length = 0;
            in_block = false;
            continue;
        }
        if (line == NULL) {
            if (interactive && buffer != NULL) { /* Ctrl-D inside an input: drop it and leave */
                free(buffer);
                return NULL;
            }
            return buffer;                       /* NULL when nothing was typed */
        }
        bool blank = line_is_blank(line);
        if (interactive && !blank) rt_history_add(line);
        buffer = buffer_append(buffer, &length, line, line_length);
        free(line);
        if (in_block && !blank) continue;        /* collecting the block: no need to look */

        Source src;
        src.file = "<repl>";
        src.text = buffer;
        src.length = length;
        InputState state;
        lexer_input_state(&src, &state);
        if (state.opened_block) in_block = true;
        if (state.open_bracket) continue;        /* `print((1 +` keeps reading, blank lines too */
        if (blank) return buffer;                /* ends the block, or an opener without a body */
        if (state.ends_with_colon || in_block) continue;
        return buffer;
    }
}

/* The session continues after an error; the exit code remembers that one
 * happened, as SPEC section 2.1 requires. */
static int cmd_repl(const char *version) {
    /* The line editor needs both ends of the terminal; `arete 0.1 > log`
     * reads plain lines without prompts, like a pipe. */
    bool interactive = rt_stdin_is_terminal() && rt_stdout_is_terminal();
    int status = 0;
    Session s;
    session_init(&s, true);
    if (interactive) {
        printf("Arete %s REPL\n", version);
        rt_history_load();
    }
    for (;;) {
        char *input = read_input(interactive);
        if (input == NULL) break;
        Source *src = source_new("<repl>", input, input_length(input));
        if (run_source(&s, src, true, MODE_RUN) != 0) status = 1;
    }
    if (interactive) rt_history_save();
    session_free(&s);
    return status;
}

static int unexpected_argument(const char *arg) {
    fprintf(stderr, "error: unexpected argument `%s`\n", arg);
    size_t n = strlen(arg);
    if (n > 3 && strcmp(arg + n - 3, ".rt") == 0) {
        fprintf(stderr, "  to run a file: arete run %s\n", arg);
    } else {
        print_usage(stderr);
    }
    return 1;
}

int main(int argc, char **argv) {
    rt_sys_init();
    if (argc < 2) {
        fprintf(stderr, "error: version required\n");
        print_usage(stderr);
        return 1;
    }
    const char *arg = argv[1];
    if (strcmp(arg, "run") == 0) {
        Mode mode = MODE_RUN;
        int i = 2;
        if (i < argc && strcmp(argv[i], "--dump-tokens") == 0) { mode = MODE_DUMP_TOKENS; i++; }
        else if (i < argc && strcmp(argv[i], "--dump-ast") == 0) { mode = MODE_DUMP_AST; i++; }
        if (i >= argc) {
            fprintf(stderr, "error: missing file\n  usage: arete run [--dump-tokens | --dump-ast] <file.rt>\n");
            return 1;
        }
        if (i + 1 < argc) return unexpected_argument(argv[i + 1]);
        return cmd_run(argv[i], mode);
    }
    if (version_is_installed(arg)) {
        if (argc > 2) return unexpected_argument(argv[2]);
        return cmd_repl(arg);
    }
    if (version_looks_like(arg)) {
        fprintf(stderr, "error: Arete %s is not installed\n  run: arete install %s\n", arg, arg);
        return 1;
    }
    fprintf(stderr, "error: unknown command `%s`\n", arg);
    print_usage(stderr);
    return 1;
}
