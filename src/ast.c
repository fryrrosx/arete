#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (p == NULL) {
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    return p;
}

static char *copy_string(const char *s, size_t n) {
    char *c = xmalloc(n + 1);
    memcpy(c, s, n);
    c[n] = '\0';
    return c;
}

Expr *expr_new_string(int line, int column, const char *chars, size_t length) {
    Expr *e = xmalloc(sizeof *e);
    e->kind = EXPR_STRING;
    e->line = line;
    e->column = column;
    e->as.string.chars = copy_string(chars, length);
    e->as.string.length = length;
    return e;
}

Expr *expr_new_name(int line, int column, const char *name) {
    Expr *e = xmalloc(sizeof *e);
    e->kind = EXPR_NAME;
    e->line = line;
    e->column = column;
    e->as.name.name = copy_string(name, strlen(name));
    return e;
}

Expr *expr_new_call(int line, int column, char *name, Expr **args, size_t arg_count) {
    Expr *e = xmalloc(sizeof *e);
    e->kind = EXPR_CALL;
    e->line = line;
    e->column = column;
    e->as.call.name = name;
    e->as.call.args = args;
    e->as.call.arg_count = arg_count;
    return e;
}

void expr_free(Expr *e) {
    if (e == NULL) return;
    switch (e->kind) {
    case EXPR_STRING:
        free(e->as.string.chars);
        break;
    case EXPR_NAME:
        free(e->as.name.name);
        break;
    case EXPR_CALL:
        free(e->as.call.name);
        for (size_t i = 0; i < e->as.call.arg_count; i++) expr_free(e->as.call.args[i]);
        free(e->as.call.args);
        break;
    }
    free(e);
}

Stmt *stmt_new_expr(Expr *e) {
    Stmt *s = xmalloc(sizeof *s);
    s->kind = STMT_EXPR;
    s->line = e->line;
    s->column = e->column;
    s->expr = e;
    return s;
}

void stmt_free(Stmt *s) {
    if (s == NULL) return;
    expr_free(s->expr);
    free(s);
}

Program *program_new(void) {
    Program *p = xmalloc(sizeof *p);
    p->stmts = NULL;
    p->count = p->capacity = 0;
    return p;
}

void program_add(Program *p, Stmt *s) {
    if (p->count == p->capacity) {
        size_t cap = p->capacity == 0 ? 16 : p->capacity * 2;
        Stmt **grown = realloc(p->stmts, cap * sizeof(Stmt *));
        if (grown == NULL) {
            fputs("fatal: out of memory\n", stderr);
            exit(2);
        }
        p->stmts = grown;
        p->capacity = cap;
    }
    p->stmts[p->count++] = s;
}

void program_free(Program *p) {
    if (p == NULL) return;
    for (size_t i = 0; i < p->count; i++) stmt_free(p->stmts[i]);
    free(p->stmts);
    free(p);
}
