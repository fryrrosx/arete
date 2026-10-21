#include "check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;      /* owned */
    bool is_mutable;
    int line;        /* where it was declared */
} Decl;

typedef struct Scope {
    struct Scope *parent;
    Decl *items;
    size_t count;
    size_t capacity;
} Scope;

struct Checker {
    bool repl_mode;
    Scope *scope;    /* innermost; the global scope is the last parent */
};

/* ---- scopes ----------------------------------------------------------- */

static Scope *scope_new(Scope *parent) {
    Scope *s = calloc(1, sizeof *s);
    if (s == NULL) {
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    s->parent = parent;
    return s;
}

static void scope_free(Scope *s) {
    for (size_t i = 0; i < s->count; i++) free(s->items[i].name);
    free(s->items);
    free(s);
}

static Decl *scope_find_here(Scope *s, const char *name) {
    for (size_t i = 0; i < s->count; i++) {
        if (strcmp(s->items[i].name, name) == 0) return &s->items[i];
    }
    return NULL;
}

static Decl *scope_find(Scope *s, const char *name) {
    for (; s != NULL; s = s->parent) {
        Decl *d = scope_find_here(s, name);
        if (d != NULL) return d;
    }
    return NULL;
}

static void scope_declare(Scope *s, const char *name, bool is_mutable, int line) {
    Decl *existing = scope_find_here(s, name);
    if (existing != NULL) {                 /* only the REPL gets here */
        existing->is_mutable = is_mutable;
        existing->line = line;
        return;
    }
    if (s->count == s->capacity) {
        size_t cap = s->capacity == 0 ? 8 : s->capacity * 2;
        Decl *grown = realloc(s->items, cap * sizeof(Decl));
        if (grown == NULL) {
            fputs("fatal: out of memory\n", stderr);
            exit(2);
        }
        s->items = grown;
        s->capacity = cap;
    }
    Decl *d = &s->items[s->count++];
    size_t n = strlen(name);
    d->name = malloc(n + 1);
    if (d->name == NULL) {
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    memcpy(d->name, name, n + 1);
    d->is_mutable = is_mutable;
    d->line = line;
}

Checker *checker_new(bool repl_mode) {
    Checker *c = calloc(1, sizeof *c);
    if (c == NULL) {
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    c->repl_mode = repl_mode;
    c->scope = scope_new(NULL);
    return c;
}

void checker_free(Checker *c) {
    if (c == NULL) return;
    while (c->scope != NULL) {
        Scope *parent = c->scope->parent;
        scope_free(c->scope);
        c->scope = parent;
    }
    free(c);
}

/* ---- expressions ------------------------------------------------------ */

static bool check_expr(Checker *c, const Expr *e, bool used_as_value, Diag *err) {
    switch (e->kind) {
    case EXPR_INT:
    case EXPR_FLOAT:
    case EXPR_STRING:
    case EXPR_BOOL:
        return true;

    case EXPR_NAME:
        if (scope_find(c->scope, e->as.name.name) == NULL) {
            char summary[160];
            snprintf(summary, sizeof summary, "unknown name `%s`", e->as.name.name);
            diag_set(err, e->line, e->column, summary, "nothing has this name",
                     "declare it first: `let name = ...`; to call a function write `name(...)`");
            return false;
        }
        return true;

    case EXPR_CALL: {
        const char *name = e->as.call.name;
        if (strcmp(name, "print") != 0) {
            char summary[160];
            snprintf(summary, sizeof summary, "unknown function `%s`", name);
            diag_set(err, e->line, e->column, summary, "no function has this name",
                     "only `print` exists so far");
            return false;
        }
        if (used_as_value) {
            diag_set(err, e->line, e->column,
                     "`print` returns nothing and cannot be used as a value",
                     "used as a value here", "call it on its own line");
            return false;
        }
        if (e->as.call.arg_count != 1) {
            char label[80];
            snprintf(label, sizeof label, "called with %zu argument%s",
                     e->as.call.arg_count, e->as.call.arg_count == 1 ? "" : "s");
            diag_set(err, e->line, e->column, "print takes exactly one argument",
                     label, "write print(x)");
            return false;
        }
        return check_expr(c, e->as.call.args[0], true, err);
    }

    case EXPR_UNARY:
        return check_expr(c, e->as.unary.operand, true, err);
    case EXPR_BINARY:
        return check_expr(c, e->as.binary.left, true, err)
            && check_expr(c, e->as.binary.right, true, err);
    case EXPR_LOGICAL:
        return check_expr(c, e->as.logical.left, true, err)
            && check_expr(c, e->as.logical.right, true, err);
    case EXPR_TERNARY:
        return check_expr(c, e->as.ternary.cond, true, err)
            && check_expr(c, e->as.ternary.then, true, err)
            && check_expr(c, e->as.ternary.otherwise, true, err);
    }
    return true;
}

/* ---- statements ------------------------------------------------------- */

static bool check_block(Checker *c, const StmtList *block, Diag *err);

static bool check_stmt(Checker *c, const Stmt *s, Diag *err) {
    switch (s->kind) {
    case STMT_EXPR:
        return check_expr(c, s->as.expr, false, err);

    case STMT_LET: {
        /* The value is checked first: `let x = x` must not see the new x. */
        if (!check_expr(c, s->as.let.value, true, err)) return false;
        Decl *here = scope_find_here(c->scope, s->as.let.name);
        bool at_top = (c->scope->parent == NULL);
        if (here != NULL && !(c->repl_mode && at_top)) {
            char summary[160], label[80];
            snprintf(summary, sizeof summary, "`%s` is already declared", s->as.let.name);
            snprintf(label, sizeof label, "second declaration; the first is on line %d", here->line);
            diag_set(err, s->line, s->column, summary, label,
                     here->is_mutable ? "assign to it instead: `name = value`"
                                      : "use a new name, or declare it with `var` and assign to it");
            return false;
        }
        scope_declare(c->scope, s->as.let.name, s->as.let.is_mutable, s->line);
        return true;
    }

    case STMT_ASSIGN: {
        Decl *d = scope_find(c->scope, s->as.assign.name);
        if (d == NULL) {
            char summary[160], help[160];
            snprintf(summary, sizeof summary, "unknown name `%s`", s->as.assign.name);
            snprintf(help, sizeof help, "declare it first: `var %s = ...`", s->as.assign.name);
            diag_set(err, s->line, s->column, summary, "nothing has this name", help);
            return false;
        }
        if (!d->is_mutable) {
            char summary[160];
            snprintf(summary, sizeof summary, "cannot assign to immutable `%s`", s->as.assign.name);
            diag_set(err, s->line, s->column, summary, "assigned here",
                     "declare it with `var` if it must change");
            return false;
        }
        return check_expr(c, s->as.assign.value, true, err);
    }

    case STMT_IF:
        for (size_t i = 0; i < s->as.if_.count; i++) {
            if (!check_expr(c, s->as.if_.conds[i], true, err)) return false;
            if (!check_block(c, s->as.if_.blocks[i], err)) return false;
        }
        if (s->as.if_.otherwise != NULL && !check_block(c, s->as.if_.otherwise, err)) return false;
        return true;
    }
    return true;
}

/* A block is a scope: names declared inside it end with it. */
static bool check_block(Checker *c, const StmtList *block, Diag *err) {
    c->scope = scope_new(c->scope);
    bool ok = true;
    for (size_t i = 0; ok && i < block->count; i++) ok = check_stmt(c, block->items[i], err);
    Scope *inner = c->scope;
    c->scope = inner->parent;
    scope_free(inner);
    return ok;
}

bool check_program(Checker *c, const Program *prog, Diag *err) {
    for (size_t i = 0; i < prog->body.count; i++) {
        if (!check_stmt(c, prog->body.items[i], err)) return false;
    }
    return true;
}
