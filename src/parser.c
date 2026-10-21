#include "parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const TokenList *tokens;
    size_t pos;
    Diag *err;
} Parser;

/* ---- helpers ---------------------------------------------------------- */

static const Token *peek(const Parser *p) {
    return &p->tokens->items[p->pos];
}

static const Token *advance(Parser *p) {
    const Token *t = peek(p);
    if (t->kind != TOK_EOF) p->pos++;
    return t;
}

static bool peek_is(const Parser *p, TokenKind kind) {
    return peek(p)->kind == kind;
}

static void error_at(Parser *p, const Token *t, const char *summary,
                     const char *label, const char *help) {
    diag_set(p->err, t->line, t->column, summary, label, help);
}

/* Consumes a token of `kind` or reports "expected X, found Y". */
static bool expect(Parser *p, TokenKind kind, const char *what, const char *help) {
    const Token *t = peek(p);
    if (t->kind == kind) {
        advance(p);
        return true;
    }
    char summary[160];
    snprintf(summary, sizeof summary, "expected %s, found %s",
             what, token_kind_describe(t->kind));
    error_at(p, t, summary, "here", help);
    return false;
}

/* ---- rules ------------------------------------------------------------ */

static Expr *parse_expression(Parser *p);

/* arguments = expression { "," expression } ;
 * call      = "(" [ arguments ] ")" ;
 * The "(" has been seen by the caller, not consumed. */
static Expr *parse_call(Parser *p, Expr *callee) {
    const Token *open = advance(p);                 /* the "(" */
    if (callee->kind != EXPR_NAME) {
        error_at(p, open, "only a name can be called",
                 "this is not a function name", NULL);
        expr_free(callee);
        return NULL;
    }
    Expr **args = NULL;
    size_t count = 0, capacity = 0;
    if (!peek_is(p, TOK_RPAREN)) {
        for (;;) {
            Expr *arg = parse_expression(p);
            if (arg == NULL) goto fail;
            if (count == capacity) {
                capacity = capacity == 0 ? 4 : capacity * 2;
                Expr **grown = realloc(args, capacity * sizeof(Expr *));
                if (grown == NULL) {
                    fputs("fatal: out of memory\n", stderr);
                    exit(2);
                }
                args = grown;
            }
            args[count++] = arg;
            if (peek_is(p, TOK_COMMA)) {
                advance(p);
                continue;
            }
            break;
        }
    }
    if (!expect(p, TOK_RPAREN, "`)`", "arguments are separated by `,`")) goto fail;

    char *name = callee->as.name.name;             /* move the name out */
    callee->as.name.name = NULL;
    int line = callee->line, column = callee->column;
    expr_free(callee);
    return expr_new_call(line, column, name, args, count);

fail:
    for (size_t i = 0; i < count; i++) expr_free(args[i]);
    free(args);
    expr_free(callee);
    return NULL;
}

/* primary = STRING | IDENT ;        (phase 4 adds INT, FLOAT, "(" expression ")") */
static Expr *parse_primary(Parser *p) {
    const Token *t = peek(p);
    switch (t->kind) {
    case TOK_STRING:
        advance(p);
        return expr_new_string(t->line, t->column, t->text, t->length);
    case TOK_IDENT:
        advance(p);
        return expr_new_name(t->line, t->column, t->text);
    case TOK_INT:
        error_at(p, t, "numbers arrive in phase 4", "a number here",
                 "for now only strings can be printed");
        return NULL;
    default: {
        char summary[160];
        snprintf(summary, sizeof summary, "expected an expression, found %s",
                 token_kind_describe(t->kind));
        error_at(p, t, summary, "here", NULL);
        return NULL;
    }
    }
}

/* postfix = primary { call } ;       (phase 3: the callee must be a name) */
static Expr *parse_postfix(Parser *p) {
    Expr *e = parse_primary(p);
    while (e != NULL && peek_is(p, TOK_LPAREN)) e = parse_call(p, e);
    return e;
}

/* expression = postfix ;            (phase 4 puts the Pratt loop here) */
static Expr *parse_expression(Parser *p) {
    return parse_postfix(p);
}

/* expr_stmt = expression NEWLINE ; */
static Stmt *parse_expr_stmt(Parser *p) {
    Expr *e = parse_expression(p);
    if (e == NULL) return NULL;
    const char *help = NULL;
    if (peek_is(p, TOK_STRING) && e->kind == EXPR_NAME) {
        help = "a call needs parentheses: print(\"...\")";
    }
    if (!expect(p, TOK_NEWLINE, "end of line", help)) {
        expr_free(e);
        return NULL;
    }
    return stmt_new_expr(e);
}

/* statement = expr_stmt ;           (phase 5 adds let_stmt and assign_stmt) */
static Stmt *parse_statement(Parser *p) {
    return parse_expr_stmt(p);
}

/* program = HEADER { statement } EOF ; */
Program *parse_program(const TokenList *tokens, bool repl_mode, Diag *err) {
    Parser p;
    p.tokens = tokens;
    p.pos = 0;
    p.err = err;

    Program *prog = program_new();
    if (!repl_mode) {
        /* The lexer guarantees that a file's first token is the header. */
        const Token *header = advance(&p);
        snprintf(prog->edition, sizeof prog->edition, "%s", header->text);
    }
    while (!peek_is(&p, TOK_EOF)) {
        Stmt *s = parse_statement(&p);
        if (s == NULL) {
            program_free(prog);
            return NULL;
        }
        program_add(prog, s);
    }
    return prog;
}
