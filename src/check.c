/* check.c - the resolver (Crafting Interpreters chapter 11, phase 7): a
 * static pass between parsing and running. It checks that every name is
 * declared before use, that `let`, parameters and functions are never
 * assigned, that a name is declared once per scope, that `return`,
 * `break` and `continue` appear where they may, and that calls to known
 * functions pass the right arguments. On the way it numbers every local
 * name as (hops, slot) and writes that into the tree, so the interpreter
 * never searches a scope by name except the global one. */
#include "check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

typedef enum { DECL_VAR, DECL_PARAM, DECL_FN, DECL_BUILTIN } DeclKind;

typedef struct {
    char *name;             /* owned */
    DeclKind kind;
    bool is_mutable;
    int line;               /* where it was declared */
    int slot;               /* SCOPE_GLOBAL in the global scope */
    const FnProto *proto;   /* DECL_FN: for static argument checks */
    bool returns_value;     /* DECL_FN and DECL_BUILTIN */
} Decl;

/* One scope at check time; the interpreter creates one ObjEnv for each. */
typedef struct Scope {
    struct Scope *parent;
    Decl *items;
    size_t count;
    size_t capacity;
    int slot_count;         /* slots handed out so far */
    bool is_global;
} Scope;

/* The function whose body is being checked (NULL proto at top level). */
typedef struct FnCtx {
    const FnProto *proto;
    bool returns_value;
    int loop_depth;         /* loops open inside this function */
    struct FnCtx *outer;
} FnCtx;

struct Checker {
    bool repl_mode;
    Scope *scope;           /* innermost; the global scope is the last parent */
    FnCtx *fn;
};

/* ---- scopes ----------------------------------------------------------- */

static Scope *scope_new(Scope *parent) {
    Scope *s = xcalloc(sizeof *s);
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

/* Finds a name and how many scopes outwards it lives (SCOPE_GLOBAL for
 * the global scope, which is addressed by name at run time). */
static Decl *resolve(Checker *c, const char *name, int *hops) {
    int h = 0;
    for (Scope *s = c->scope; s != NULL; s = s->parent, h++) {
        Decl *d = scope_find_here(s, name);
        if (d != NULL) {
            *hops = s->is_global ? SCOPE_GLOBAL : h;
            return d;
        }
    }
    return NULL;
}

static Decl *scope_declare(Scope *s, const char *name, DeclKind kind, bool is_mutable, int line) {
    Decl *existing = scope_find_here(s, name);
    if (existing != NULL) {                 /* only the REPL's global scope gets here */
        existing->kind = kind;
        existing->is_mutable = is_mutable;
        existing->line = line;
        existing->proto = NULL;
        existing->returns_value = false;
        return existing;
    }
    if (s->count == s->capacity) {
        s->capacity = s->capacity == 0 ? 8 : s->capacity * 2;
        s->items = xrealloc(s->items, s->capacity * sizeof(Decl));
    }
    Decl *d = &s->items[s->count++];
    d->name = xstrdup(name);
    d->kind = kind;
    d->is_mutable = is_mutable;
    d->line = line;
    d->slot = s->is_global ? SCOPE_GLOBAL : s->slot_count++;
    d->proto = NULL;
    d->returns_value = false;
    return d;
}

static void push_scope(Checker *c) {
    c->scope = scope_new(c->scope);
}

static int pop_scope(Checker *c) {
    Scope *inner = c->scope;
    int slots = inner->slot_count;
    c->scope = inner->parent;
    scope_free(inner);
    return slots;
}

Checker *checker_new(bool repl_mode) {
    Checker *c = xcalloc(sizeof *c);
    c->repl_mode = repl_mode;
    c->scope = scope_new(NULL);
    c->scope->is_global = true;
    Decl *print = scope_declare(c->scope, "print", DECL_BUILTIN, false, 0);
    print->returns_value = false;
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

/* ---- errors ----------------------------------------------------------- */

static bool already_declared(const Decl *first, const char *name, int line, int column, Diag *err) {
    char summary[160], label[80];
    snprintf(summary, sizeof summary, "`%s` is already declared", name);
    snprintf(label, sizeof label, "second declaration; the first is on line %d", first->line);
    const char *help = "use a new name, or declare it with `var` and assign to it";
    if (first->kind == DECL_FN) help = "a function and a variable cannot share a name in one scope";
    else if (first->is_mutable) help = "assign to it instead: `name = value`";
    diag_set(err, line, column, summary, label, help);
    return false;
}

/* ---- expressions ------------------------------------------------------ */

static bool check_expr(Checker *c, Expr *e, bool used_as_value, Diag *err);
static bool check_function(Checker *c, FnProto *f, Diag *err);

static const char *fn_display_name(const FnProto *f) {
    return f->name != NULL ? f->name : "the lambda";
}

/* Checks the arguments of a call against a known signature: the same rules
 * the interpreter applies at run time to a function value, reported
 * before the program runs when the callee is a declared function. */
static bool check_arguments(const Expr *e, const FnProto *f, Diag *err) {
    const char *name = fn_display_name(f);
    size_t positional = 0;
    bool *filled = xcalloc((f->param_count + 1) * sizeof(bool));
    bool ok = true;
    for (size_t i = 0; ok && i < e->as.call.arg_count; i++) {
        const Expr *arg = e->as.call.args[i];
        const char *arg_name = e->as.call.names[i];
        if (arg_name == NULL) {
            if (positional >= f->param_count) {
                char summary[160], label[120];
                snprintf(summary, sizeof summary, "too many arguments to `%s`", name);
                snprintf(label, sizeof label, "it takes %zu, this is number %zu",
                         f->param_count, positional + 1);
                diag_set(err, arg->line, arg->column, summary, label, NULL);
                ok = false;
                break;
            }
            filled[positional++] = true;
            continue;
        }
        size_t index = f->param_count;
        for (size_t k = 0; k < f->param_count; k++) {
            if (strcmp(f->params[k].name, arg_name) == 0) { index = k; break; }
        }
        if (index == f->param_count) {
            char summary[160], help[200];
            snprintf(summary, sizeof summary, "`%s` has no parameter `%s`", name, arg_name);
            size_t n = snprintf(help, sizeof help, "its parameters are:");
            for (size_t k = 0; k < f->param_count && n < sizeof help; k++) {
                n += snprintf(help + n, sizeof help - n, "%s `%s`", k > 0 ? "," : "", f->params[k].name);
            }
            if (f->param_count == 0) snprintf(help, sizeof help, "it takes no arguments");
            diag_set(err, arg->line, arg->column, summary, "named argument here", help);
            ok = false;
            break;
        }
        if (filled[index]) {
            char summary[160];
            snprintf(summary, sizeof summary, "argument `%s` is given twice", arg_name);
            diag_set(err, arg->line, arg->column, summary, "second time here",
                     "it was already passed by position");
            ok = false;
            break;
        }
        filled[index] = true;
    }
    for (size_t k = 0; ok && k < f->param_count; k++) {
        if (!filled[k] && f->params[k].default_value == NULL) {
            char summary[160], help[160];
            snprintf(summary, sizeof summary, "missing argument `%s` in call to `%s`",
                     f->params[k].name, name);
            snprintf(help, sizeof help, "pass it by position or by name: %s = ...", f->params[k].name);
            diag_set(err, e->line, e->column, summary, "call here", help);
            ok = false;
        }
    }
    free(filled);
    return ok;
}

static bool returns_nothing_error(const Expr *e, const char *name, Diag *err) {
    char summary[160];
    snprintf(summary, sizeof summary, "`%s` returns nothing and cannot be used as a value", name);
    diag_set(err, e->line, e->column, summary, "used as a value here", "call it on its own line");
    return false;
}

static bool check_call(Checker *c, Expr *e, bool used_as_value, Diag *err) {
    Expr *callee = e->as.call.callee;
    const Decl *known = NULL;
    if (callee->kind == EXPR_NAME) {
        int hops;
        known = resolve(c, callee->as.name.name, &hops);
        if (known == NULL) {
            char summary[160], help[200];
            snprintf(summary, sizeof summary, "unknown function `%s`", callee->as.name.name);
            snprintf(help, sizeof help, "declare it first: `fn %s(...):`", callee->as.name.name);
            diag_set(err, callee->line, callee->column, summary, "no function has this name", help);
            return false;
        }
        callee->as.name.hops = hops;
        callee->as.name.slot = known->slot;
    } else if (!check_expr(c, callee, true, err)) {
        return false;
    }

    if (known != NULL && known->kind == DECL_BUILTIN) {           /* print */
        if (used_as_value) return returns_nothing_error(e, known->name, err);
        if (e->as.call.arg_count != 1 || e->as.call.names[0] != NULL) {
            char label[80];
            snprintf(label, sizeof label, "called with %zu argument%s",
                     e->as.call.arg_count, e->as.call.arg_count == 1 ? "" : "s");
            diag_set(err, e->line, e->column, "print takes exactly one argument", label, "write print(x)");
            return false;
        }
    } else if (known != NULL && known->kind == DECL_FN) {
        if (used_as_value && !known->returns_value) return returns_nothing_error(e, known->name, err);
        if (!check_arguments(e, known->proto, err)) return false;
    }
    for (size_t i = 0; i < e->as.call.arg_count; i++) {
        if (!check_expr(c, e->as.call.args[i], true, err)) return false;
    }
    return true;
}

static bool check_expr(Checker *c, Expr *e, bool used_as_value, Diag *err) {
    switch (e->kind) {
    case EXPR_INT:
    case EXPR_FLOAT:
    case EXPR_STRING:
    case EXPR_BOOL:
        return true;

    case EXPR_NAME: {
        if (strcmp(e->as.name.name, "_") == 0) {
            diag_set(err, e->line, e->column, "`_` cannot be read",
                     "`_` discards a value", "give it a real name if you need the value");
            return false;
        }
        int hops;
        Decl *d = resolve(c, e->as.name.name, &hops);
        if (d == NULL) {
            char summary[160];
            snprintf(summary, sizeof summary, "unknown name `%s`", e->as.name.name);
            diag_set(err, e->line, e->column, summary, "nothing has this name",
                     "declare it first: `let name = ...`; to call a function write `name(...)`");
            return false;
        }
        e->as.name.hops = hops;
        e->as.name.slot = d->slot;
        return true;
    }

    case EXPR_CALL:
        return check_call(c, e, used_as_value, err);

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
    case EXPR_RANGE:
        return check_expr(c, e->as.range.start, true, err)
            && check_expr(c, e->as.range.end, true, err);
    case EXPR_LAMBDA:
        return check_function(c, e->as.lambda.proto, err);
    }
    return true;
}

/* ---- functions -------------------------------------------------------- */

static bool check_stmt(Checker *c, Stmt *s, Diag *err);
static bool declare_functions(Checker *c, StmtList *list, Diag *err);
static bool check_list(Checker *c, StmtList *list, Diag *err);

/* The body of a fn or a lambda: one scope holding the parameters and the
 * locals declared directly in the body. A default value may only use the
 * parameters to its left, so each one is checked before its own
 * parameter is declared. */
static bool check_function(Checker *c, FnProto *f, Diag *err) {
    FnCtx ctx;
    ctx.proto = f;
    ctx.returns_value = (f->expr_body != NULL) || (f->result != NULL);
    ctx.loop_depth = 0;
    ctx.outer = c->fn;
    c->fn = &ctx;
    push_scope(c);
    bool ok = true;
    for (size_t i = 0; ok && i < f->param_count; i++) {
        Param *p = &f->params[i];
        if (p->default_value != NULL) ok = check_expr(c, p->default_value, true, err);
        if (ok) scope_declare(c->scope, p->name, DECL_PARAM, false, p->line);
    }
    if (ok && f->body != NULL) ok = declare_functions(c, f->body, err) && check_list(c, f->body, err);
    if (ok && f->expr_body != NULL) ok = check_expr(c, f->expr_body, true, err);
    f->slot_count = pop_scope(c);
    if (f->body != NULL) f->body->slot_count = f->slot_count;
    c->fn = ctx.outer;
    return ok;
}

/* Functions are visible in their whole scope, so every `fn` of a statement
 * list is declared before the list is checked (and, at run time, created
 * before the list runs). */
static bool declare_functions(Checker *c, StmtList *list, Diag *err) {
    for (size_t i = 0; i < list->count; i++) {
        Stmt *s = list->items[i];
        if (s->kind != STMT_FN) continue;
        FnProto *f = s->as.fn.proto;
        Decl *here = scope_find_here(c->scope, f->name);
        if (here != NULL && !(c->repl_mode && c->scope->is_global)) {
            return already_declared(here, f->name, s->line, s->column, err);
        }
        Decl *d = scope_declare(c->scope, f->name, DECL_FN, false, s->line);
        d->proto = f;
        d->returns_value = (f->result != NULL);
        s->as.fn.slot = d->slot;
    }
    return true;
}

/* ---- statements ------------------------------------------------------- */

static bool check_block(Checker *c, StmtList *block, Diag *err);

static bool check_stmt(Checker *c, Stmt *s, Diag *err) {
    switch (s->kind) {
    case STMT_EXPR:
        return check_expr(c, s->as.expr, false, err);

    case STMT_LET: {
        /* The value is checked first: `let x = x` must not see the new x. */
        if (!check_expr(c, s->as.let.value, true, err)) return false;
        if (strcmp(s->as.let.name, "_") == 0) {
            s->as.let.slot = SCOPE_GLOBAL;     /* `_` is never stored */
            return true;
        }
        Decl *here = scope_find_here(c->scope, s->as.let.name);
        if (here != NULL && !(c->repl_mode && c->scope->is_global)) {
            return already_declared(here, s->as.let.name, s->line, s->column, err);
        }
        Decl *d = scope_declare(c->scope, s->as.let.name, DECL_VAR, s->as.let.is_mutable, s->line);
        s->as.let.slot = d->slot;
        return true;
    }

    case STMT_ASSIGN: {
        int hops;
        Decl *d = resolve(c, s->as.assign.name, &hops);
        if (d == NULL) {
            char summary[160], help[160];
            snprintf(summary, sizeof summary, "unknown name `%s`", s->as.assign.name);
            snprintf(help, sizeof help, "declare it first: `var %s = ...`", s->as.assign.name);
            diag_set(err, s->line, s->column, summary, "nothing has this name", help);
            return false;
        }
        if (!d->is_mutable) {
            char summary[160], help[200];
            snprintf(summary, sizeof summary, "cannot assign to immutable `%s`", s->as.assign.name);
            switch (d->kind) {
            case DECL_PARAM:
                snprintf(help, sizeof help, "parameters cannot be reassigned; copy it first: `var %s2 = %s`",
                         s->as.assign.name, s->as.assign.name);
                break;
            case DECL_FN:
            case DECL_BUILTIN:
                snprintf(help, sizeof help, "`%s` is a function", s->as.assign.name);
                break;
            default:
                snprintf(help, sizeof help, "declare it with `var` if it must change");
                break;
            }
            diag_set(err, s->line, s->column, summary, "assigned here", help);
            return false;
        }
        s->as.assign.hops = hops;
        s->as.assign.slot = d->slot;
        return check_expr(c, s->as.assign.value, true, err);
    }

    case STMT_IF:
        for (size_t i = 0; i < s->as.if_.count; i++) {
            if (!check_expr(c, s->as.if_.conds[i], true, err)) return false;
            if (!check_block(c, s->as.if_.blocks[i], err)) return false;
        }
        if (s->as.if_.otherwise != NULL && !check_block(c, s->as.if_.otherwise, err)) return false;
        return true;

    case STMT_WHILE: {
        if (!check_expr(c, s->as.while_.cond, true, err)) return false;
        c->fn->loop_depth++;
        bool ok = check_block(c, s->as.while_.body, err);
        c->fn->loop_depth--;
        return ok;
    }

    case STMT_FOR: {
        if (!check_expr(c, s->as.for_.iterable, true, err)) return false;
        /* The loop variable is slot 0 of the body's scope, a fresh `let`
         * on every iteration; the body's own statements follow it. */
        push_scope(c);
        if (strcmp(s->as.for_.name, "_") == 0) {
            c->scope->slot_count++;                    /* reserve slot 0, declare nothing */
        } else {
            scope_declare(c->scope, s->as.for_.name, DECL_VAR, false, s->as.for_.name_line);
        }
        c->fn->loop_depth++;
        bool ok = declare_functions(c, s->as.for_.body, err) && check_list(c, s->as.for_.body, err);
        c->fn->loop_depth--;
        s->as.for_.body->slot_count = pop_scope(c);
        return ok;
    }

    case STMT_BREAK:
    case STMT_CONTINUE: {
        const char *word = s->kind == STMT_BREAK ? "break" : "continue";
        if (c->fn->loop_depth == 0) {
            char summary[80], help[120];
            snprintf(summary, sizeof summary, "`%s` outside a loop", word);
            snprintf(help, sizeof help, "`%s` only works inside `while` or `for`", word);
            diag_set(err, s->line, s->column, summary, "here", help);
            return false;
        }
        return true;
    }

    case STMT_RETURN: {
        if (c->fn->proto == NULL) {
            diag_set(err, s->line, s->column, "`return` outside a function", "here",
                     "`return` only works inside `fn`");
            return false;
        }
        const char *name = fn_display_name(c->fn->proto);
        if (s->as.return_.value != NULL && !c->fn->returns_value) {
            char summary[160];
            snprintf(summary, sizeof summary, "`%s` returns nothing", name);
            diag_set(err, s->line, s->column, summary, "but a value is returned here",
                     "add `-> type` after the parameters, or write a bare `return`");
            return false;
        }
        if (s->as.return_.value == NULL && c->fn->returns_value) {
            char summary[160];
            snprintf(summary, sizeof summary, "`%s` must return a value", name);
            diag_set(err, s->line, s->column, summary, "`return` without a value here",
                     "write `return value`");
            return false;
        }
        if (s->as.return_.value != NULL) return check_expr(c, s->as.return_.value, true, err);
        return true;
    }

    case STMT_FN:
        /* Declared by declare_functions; only the body is left to check. */
        return check_function(c, s->as.fn.proto, err);
    }
    return true;
}

static bool check_list(Checker *c, StmtList *list, Diag *err) {
    for (size_t i = 0; i < list->count; i++) {
        if (!check_stmt(c, list->items[i], err)) return false;
    }
    return true;
}

/* A block is a scope: names declared inside it end with it. */
static bool check_block(Checker *c, StmtList *block, Diag *err) {
    push_scope(c);
    bool ok = declare_functions(c, block, err) && check_list(c, block, err);
    block->slot_count = pop_scope(c);
    return ok;
}

bool check_program(Checker *c, Program *prog, Diag *err) {
    FnCtx top;
    top.proto = NULL;
    top.returns_value = false;
    top.loop_depth = 0;
    top.outer = NULL;
    c->fn = &top;
    bool ok = declare_functions(c, &prog->body, err) && check_list(c, &prog->body, err);
    c->fn = NULL;
    return ok;
}
