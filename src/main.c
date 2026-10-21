/* main.c - the command line:
 *   arete run <file.rt>     run a file
 *   arete <version>         start the REPL for that language version
 * Errors go to stderr; the exit code is 1 on any error. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "diag.h"
#include "interp.h"
#include "lexer.h"
#include "parser.h"
#include "rt_sys.h"
#include "version.h"

static void print_usage(FILE *out) {
    fprintf(out, "  usage: arete <version>\n");
    fprintf(out, "         arete run <file.rt>\n");
    fprintf(out, "  installed: ");
    version_print_installed(out);
    fputc('\n', out);
}

/* Lexes, parses, checks and runs one source text. Returns the exit code. */
static int run_source(const Source *src, bool repl_mode) {
    Diag err = {0};
    TokenList tokens = {0};
    Program *prog = NULL;
    int code = 1;

    if (!lexer_tokenize(src, !repl_mode, &tokens, &err)) goto done;
    prog = parse_program(&tokens, repl_mode, &err);
    if (prog == NULL) goto done;
    if (check_program(prog, &err)) code = interp_run(prog);
    program_free(prog);

done:
    if (err.is_set) diag_print(src, &err);
    token_list_free(&tokens);
    return code;
}

static int cmd_run(const char *path) {
    size_t length = 0;
    char *text = rt_read_file(path, &length);
    if (text == NULL) {
        fprintf(stderr, "error: cannot open file `%s`\n", path);
        return 1;
    }
    Source src;
    src.file = path;
    src.text = text;
    src.length = length;
    int code = run_source(&src, false);
    free(text);
    return code;
}

/* The session continues after an error; the exit code remembers that one
 * happened, as SPEC section 2.1 requires. */
static int cmd_repl(const char *version) {
    bool interactive = rt_stdin_is_terminal();
    int status = 0;
    if (interactive) printf("Arete %s REPL\n", version);
    for (;;) {
        if (interactive) {
            fputs(">>> ", stdout);
            fflush(stdout);
        }
        char *line = rt_read_line(stdin);
        if (line == NULL) {
            if (interactive) fputc('\n', stdout);
            break;
        }
        Source src;
        src.file = "<repl>";
        src.text = line;
        src.length = strlen(line);
        if (run_source(&src, true) != 0) status = 1;
        free(line);
    }
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
        if (argc < 3) {
            fprintf(stderr, "error: missing file\n  usage: arete run <file.rt>\n");
            return 1;
        }
        if (argc > 3) return unexpected_argument(argv[3]);
        return cmd_run(argv[2]);
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
