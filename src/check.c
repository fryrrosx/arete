/* check.c - the resolver and type checker: a static pass between parsing
 * and running. It checks that every name is declared before use, that
 * `let`, parameters and functions are never assigned, that a name is
 * declared once per scope, that `return`, `break` and `continue` appear
 * where they may, and that every expression has a type that fits where it
 * is used (phase 10: int, float, string, bool, range and function types;
 * annotations flow down into lambdas, literals and operators flow up). On
 * the way it numbers every local name as (hops, slot) and writes that into
 * the tree, so the interpreter never searches a scope by name except the
 * global one. */
#include "check.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt_sys.h"
#include "types.h"
#include "util.h"

typedef enum { DECL_VAR, DECL_PARAM, DECL_FN, DECL_BUILTIN } DeclKind;

typedef struct {
    char *name;             /* owned */
    DeclKind kind;
    bool is_mutable;
    int line;               /* where it was declared */
    int slot;               /* SCOPE_GLOBAL in the global scope */
    const FnProto *proto;   /* DECL_FN: for static argument checks */
    Type *type;             /* what the name holds; for a function, its signature */
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
    Type *result;           /* NULL: returns nothing */
    int loop_depth;         /* loops open inside this function */
    struct FnCtx *outer;
} FnCtx;

/* A global declaration overwritten while checking one REPL input, kept so
 * that a failed input can be undone (the run-time scope never saw it). */
typedef struct {
    size_t index;
    Decl old;               /* the name pointer is shared, not owned */
} Undo;

struct Checker {
    bool repl_mode;
    Scope *scope;           /* innermost; the global scope is the last parent */
    FnCtx *fn;
    const StmtList *top;    /* the statement list of the program being checked */
    TypeArena types;
    const char *stack_base; /* a local of check_program, for the stack guard */
    size_t stack_budget;
    Undo *undo;             /* only while checking a REPL input */
    size_t undo_count;
    size_t undo_capacity;
    size_t global_count_before;
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

static Decl *scope_declare(Checker *c, Scope *s, const char *name, DeclKind kind, bool is_mutable,
                           int line, Type *type) {
    Decl *existing = scope_find_here(s, name);
    if (existing != NULL) {                 /* only the REPL's global scope gets here */
        if (c != NULL && c->undo != NULL) {
            if (c->undo_count == c->undo_capacity) {
                c->undo_capacity = c->undo_capacity == 0 ? 4 : c->undo_capacity * 2;
                c->undo = xrealloc(c->undo, c->undo_capacity * sizeof(Undo));
            }
            c->undo[c->undo_count].index = (size_t)(existing - s->items);
            c->undo[c->undo_count].old = *existing;
            c->undo_count++;
        }
        existing->kind = kind;
        existing->is_mutable = is_mutable;
        existing->line = line;
        existing->proto = NULL;
        existing->type = type;
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
    d->type = type;
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

static Type *simple(Checker *c, TypeTag tag) {
    return type_simple(&c->types, tag);
}

Checker *checker_new(bool repl_mode) {
    Checker *c = xcalloc(sizeof *c);
    c->repl_mode = repl_mode;
    size_t limit = rt_stack_limit();
    c->stack_budget = limit > 2u * 1024u * 1024u ? limit - 1024u * 1024u : limit / 2;
    type_arena_init(&c->types);
    c->scope = scope_new(NULL);
    c->scope->is_global = true;
    Type **any = xmalloc(sizeof(Type *));
    any[0] = simple(c, TY_ANY);
    scope_declare(NULL, c->scope, "print", DECL_BUILTIN, false, 0, type_fn(&c->types, any, 1, NULL));
    return c;
}

void checker_free(Checker *c) {
    if (c == NULL) return;
    while (c->scope != NULL) {
        Scope *parent = c->scope->parent;
        scope_free(c->scope);
        c->scope = parent;
    }
    type_arena_free(&c->types);
    free(c->undo);
    free(c);
}

/* ---- errors ----------------------------------------------------------- */

static bool already_declared(const Decl *first, const char *name, int line, int column, Diag *err) {
    char summary[160], label[80];
    snprintf(summary, sizeof summary, "`%s` is already declared", name);
    if (first->kind == DECL_BUILTIN) {
        diag_set(err, line, column, summary, "it is a built-in function", "choose another name");
        return false;
    }
    snprintf(label, sizeof label, "second declaration; the first is on line %d", first->line);
    const char *help = "use a new name, or declare it with `var` and assign to it";
    if (first->kind == DECL_FN) help = "a function and a variable cannot share a name in one scope";
    else if (first->is_mutable) help = "assign to it instead: `name = value`";
    diag_set(err, line, column, summary, label, help);
    return false;
}

/* `let`/`var` are not hoisted: a function body that reads a global declared
 * further down gets told so, instead of a bare "unknown name". */
static bool unknown_name(Checker *c, const char *name, int line, int column, bool as_call, Diag *err) {
    char summary[160], label[120], help[200];
    snprintf(summary, sizeof summary, as_call ? "unknown function `%s`" : "unknown name `%s`", name);
    const Stmt *later = NULL;
    if (c->fn->proto != NULL && c->top != NULL) {
        for (size_t i = 0; i < c->top->count; i++) {
            const Stmt *s = c->top->items[i];
            if (s->kind == STMT_LET && s->line > line && strcmp(s->as.let.name, name) == 0) { later = s; break; }
        }
    }
    if (later != NULL) {
        snprintf(label, sizeof label, "declared only on line %d, below this function", later->line);
        snprintf(help, sizeof help, "move `%s %s` above the function; only functions are visible before their declaration",
                 later->as.let.is_mutable ? "var" : "let", name);
        diag_set(err, line, column, summary, label, help);
    } else if (as_call) {
        snprintf(help, sizeof help, "declare it first: `fn %s(...):`", name);
        diag_set(err, line, column, summary, "no function has this name", help);
    } else {
        diag_set(err, line, column, summary, "nothing has this name",
                 "declare it first: `let name = ...`; to call a function write `name(...)`");
    }
    return false;
}

/* "expected int, found string" at a position, with a label and help that
 * say where each type came from. */
static bool mismatch(Diag *err, int line, int column, const Type *expected, const Type *found,
                     const char *label, const char *help) {
    char want[120], have[120], summary[280];
    type_name(expected, want, sizeof want);
    type_name(found, have, sizeof have);
    snprintf(summary, sizeof summary, "expected %s, found %s", want, have);
    diag_set(err, line, column, summary, label, help);
    return false;
}

static bool cannot_apply(Diag *err, const Expr *at, TokenKind op, const Type *a, const Type *b) {
    char ta[120], tb[120], summary[300], label[260];
    type_name(a, ta, sizeof ta);
    type_name(b, tb, sizeof tb);
    snprintf(summary, sizeof summary, "cannot apply `%s` to %s and %s", op_symbol(op), ta, tb);
    snprintf(label, sizeof label, "left is %s, right is %s", ta, tb);
    const char *help = NULL;
    bool a_str = a->tag == TY_STRING, b_str = b->tag == TY_STRING;
    if (op == TOK_PLUS && a_str != b_str) help = "both sides of `+` must be strings or both numbers";
    else if (op == TOK_EQUAL_EQUAL || op == TOK_BANG_EQUAL) help = "the two sides can never be equal";
    diag_set(err, at->line, at->column, summary, label, help);
    return false;
}

static bool need_bool_type(Diag *err, const Expr *at, const Type *t, const char *what) {
    if (t->tag == TY_BOOL) return true;
    char have[120], summary[200];
    type_name(t, have, sizeof have);
    snprintf(summary, sizeof summary, "%s must be bool, found %s", what, have);
    diag_set(err, at->line, at->column, summary, "here", "compare explicitly, for example `x != 0`");
    return false;
}

static bool is_number(const Type *t) {
    return t->tag == TY_INT || t->tag == TY_FLOAT;
}

/* The parser caps expressions at 1000 levels, which fits an 8 MB stack; on
 * a smaller stack the walk stops here instead of crashing. A distance far
 * beyond any stack means the locals are not on it (a sanitizer's fake
 * stack); then there is nothing to measure. */
static bool stack_ok(Checker *c, const Expr *at, Diag *err) {
    char here;
    size_t used = (size_t)(c->stack_base > &here ? c->stack_base - &here : &here - c->stack_base);
    if (used < c->stack_budget || used > 64 * c->stack_budget) return true;
    diag_set(err, at->line, at->column, "expression nested too deeply", "here",
             "split it into smaller steps with `let`");
    return false;
}

static bool cannot_negate(Diag *err, const Expr *at, const Type *t) {
    char have[120], summary[160];
    type_name(t, have, sizeof have);
    snprintf(summary, sizeof summary, "cannot negate %s", have);
    diag_set(err, at->line, at->column, summary, "here", NULL);
    return false;
}

static bool branches_differ(Diag *err, const Expr *otherwise, const Type *a, const Type *b) {
    char ta[120], tb[120], summary[300];
    type_name(a, ta, sizeof ta);
    type_name(b, tb, sizeof tb);
    snprintf(summary, sizeof summary, "branches of a conditional have different types: %s and %s", ta, tb);
    diag_set(err, otherwise->line, otherwise->column, summary, "the `else` branch here",
             "give both branches the same type");
    return false;
}

static bool range_end_not_int(Diag *err, const Expr *bad, const Type *t) {
    char have[120], summary[160];
    type_name(t, have, sizeof have);
    snprintf(summary, sizeof summary, "range ends must be int, found %s", have);
    diag_set(err, bad->line, bad->column, summary, "this end", "a range is `start..end` over integers");
    return false;
}

/* An int used where a float is expected is converted when it arrives, so
 * `let x: float = 1` holds 1.0: the checker wraps the expression, and the
 * interpreter converts the value. `slot` is the parent's pointer to it. */
static void widen_if_needed(Expr **slot, const Type *wanted, const Type *found) {
    if (wanted == NULL || wanted->tag != TY_FLOAT || found->tag != TY_INT) return;
    Expr *w = expr_new(EXPR_WIDEN, (*slot)->line, (*slot)->column);
    w->as.widen.operand = *slot;
    *slot = w;
}

/* The type of `a op b`, or NULL with the error set. Shared by binary
 * expressions and compound assignment. */
static Type *binary_result(Checker *c, const Expr *at, TokenKind op, Type *a, Type *b, Diag *err) {
    switch (op) {
    case TOK_PLUS:
        if (a->tag == TY_STRING && b->tag == TY_STRING) return a;
        /* fall through */
    case TOK_MINUS: case TOK_STAR: case TOK_SLASH: case TOK_SLASH_SLASH: case TOK_STAR_STAR:
        if (!is_number(a) || !is_number(b)) { cannot_apply(err, at, op, a, b); return NULL; }
        if (op == TOK_SLASH) return simple(c, TY_FLOAT);          /* `/` is always a float */
        if (a->tag == TY_FLOAT || b->tag == TY_FLOAT) return simple(c, TY_FLOAT);
        return simple(c, TY_INT);
    case TOK_EQUAL_EQUAL: case TOK_BANG_EQUAL:
        if ((is_number(a) && is_number(b)) || type_equal(a, b)) return simple(c, TY_BOOL);
        cannot_apply(err, at, op, a, b);
        return NULL;
    case TOK_LESS: case TOK_LESS_EQUAL: case TOK_GREATER: case TOK_GREATER_EQUAL:
        if ((is_number(a) && is_number(b)) || (a->tag == TY_STRING && b->tag == TY_STRING)) {
            return simple(c, TY_BOOL);
        }
        cannot_apply(err, at, op, a, b);
        return NULL;
    default:
        cannot_apply(err, at, op, a, b);
        return NULL;
    }
}

/* ---- expressions ------------------------------------------------------ */

static Type *check_expr(Checker *c, Expr *e, const Type *expected, bool used_as_value, Diag *err);
static Type *check_lambda(Checker *c, FnProto *f, const Type *expected, Diag *err);
static bool check_function(Checker *c, FnProto *f, const Type *signature, Diag *err);

static const char *fn_display_name(const FnProto *f) {
    return f->name != NULL ? f->name : "the lambda";
}

/* The signature of a declared function from its annotations. */
static Type *signature_of(Checker *c, const FnProto *f, Diag *err) {
    Type **params = f->param_count > 0 ? xmalloc(f->param_count * sizeof(Type *)) : NULL;
    for (size_t i = 0; i < f->param_count; i++) {
        params[i] = type_from_expr(&c->types, f->params[i].type, err);
        if (params[i] == NULL) { free(params); return NULL; }
    }
    Type *result = NULL;
    if (f->result != NULL) {
        result = type_from_expr(&c->types, f->result, err);
        if (result == NULL) { free(params); return NULL; }
    }
    return type_fn(&c->types, params, f->param_count, result);
}

/* Checks the arguments of a call against a known signature: the same rules
 * the interpreter applies at run time to a function value, reported
 * before the program runs when the callee is a declared function. Fills
 * `which[i]` with the parameter index each argument binds to. */
static bool check_arguments(const Expr *e, const FnProto *f, size_t *which, Diag *err) {
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
            which[i] = positional;
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
        which[i] = index;
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

/* One argument against the parameter type it binds to. */
static bool check_argument(Checker *c, Expr **arg_slot, const Type *param, const char *param_name,
                           const char *fn_name, Diag *err) {
    Expr *arg = *arg_slot;
    Type *t = check_expr(c, arg, param, true, err);
    if (t == NULL) return false;
    if (type_accepts(param, t)) {
        widen_if_needed(arg_slot, param, t);
        return true;
    }
    char have[120], label[160], help[200];
    type_name(t, have, sizeof have);
    snprintf(label, sizeof label, "this argument is %s", have);
    if (param_name != NULL) {
        char want[120];
        type_name(param, want, sizeof want);
        snprintf(help, sizeof help, "parameter `%s` of `%s` is %s", param_name, fn_name, want);
    } else {
        snprintf(help, sizeof help, "the function `%s` was declared with that parameter type", fn_name);
    }
    return mismatch(err, arg->line, arg->column, param, t, label, help);
}

static Type *check_call(Checker *c, Expr *e, bool used_as_value, Diag *err) {
    Expr *callee = e->as.call.callee;
    const Decl *known = NULL;
    Type *callee_type;
    const char *callee_name = "the function";
    if (callee->kind == EXPR_NAME) {
        int hops;
        known = resolve(c, callee->as.name.name, &hops);
        if (known == NULL) {
            unknown_name(c, callee->as.name.name, callee->line, callee->column, true, err);
            return NULL;
        }
        callee->as.name.hops = hops;
        callee->as.name.slot = known->slot;
        callee_type = known->type;
        callee_name = known->name;
    } else {
        callee_type = check_expr(c, callee, NULL, true, err);
        if (callee_type == NULL) return NULL;
    }
    if (callee_type->tag != TY_FN) {
        char have[120], summary[200], label[200];
        type_name(callee_type, have, sizeof have);
        if (callee->kind == EXPR_NAME) {
            snprintf(summary, sizeof summary, "`%s` is not a function", callee_name);
            snprintf(label, sizeof label, "it holds %s", have);
        } else {
            snprintf(summary, sizeof summary, "cannot call %s", have);
            snprintf(label, sizeof label, "this is %s, not a function", have);
        }
        diag_set(err, e->line, e->column, summary, label, NULL);
        return NULL;
    }
    size_t count = e->as.call.arg_count;

    if (known != NULL && known->kind == DECL_BUILTIN) {           /* print */
        if (used_as_value) { returns_nothing_error(e, known->name, err); return NULL; }
        if (count >= 1 && e->as.call.names[0] != NULL) {
            char summary[160];
            snprintf(summary, sizeof summary, "`print` has no parameter `%s`", e->as.call.names[0]);
            diag_set(err, e->as.call.args[0]->line, e->as.call.args[0]->column, summary,
                     "named argument here", "write print(x)");
            return NULL;
        }
        if (count != 1) {
            char label[80];
            snprintf(label, sizeof label, "called with %zu argument%s", count, count == 1 ? "" : "s");
            diag_set(err, e->line, e->column, "print takes exactly one argument", label, "write print(x)");
            return NULL;
        }
        if (check_expr(c, e->as.call.args[0], NULL, true, err) == NULL) return NULL;
        return simple(c, TY_NONE);
    }

    if (known != NULL && known->kind == DECL_FN) {
        /* A declared function: names, arity and defaults are all known. */
        const FnProto *f = known->proto;
        size_t *which = count > 0 ? xmalloc(count * sizeof(size_t)) : NULL;
        bool ok = check_arguments(e, f, which, err);
        for (size_t i = 0; ok && i < count; i++) {
            ok = check_argument(c, &e->as.call.args[i], callee_type->params[which[i]],
                                f->params[which[i]].name, known->name, err);
        }
        free(which);
        if (!ok) return NULL;
    } else {
        /* A function value: the type gives the parameter types but not
         * their names or defaults; named arguments are checked when the
         * call runs. */
        size_t positional = 0;
        for (size_t i = 0; i < count; i++) {
            Expr *arg = e->as.call.args[i];
            if (e->as.call.names[i] != NULL) {
                if (check_expr(c, arg, NULL, true, err) == NULL) return NULL;
                continue;
            }
            if (positional >= callee_type->param_count) {
                char summary[160], label[120];
                snprintf(summary, sizeof summary, "too many arguments to `%s`", callee_name);
                snprintf(label, sizeof label, "it takes %zu, this is number %zu",
                         callee_type->param_count, positional + 1);
                diag_set(err, arg->line, arg->column, summary, label, NULL);
                return NULL;
            }
            if (!check_argument(c, &e->as.call.args[i], callee_type->params[positional], NULL, callee_name, err)) return NULL;
            positional++;
        }
    }
    if (callee_type->result == NULL) {
        if (used_as_value) { returns_nothing_error(e, callee_name, err); return NULL; }
        return simple(c, TY_NONE);
    }
    return callee_type->result;
}

static Type *check_expr(Checker *c, Expr *e, const Type *expected, bool used_as_value, Diag *err) {
    if (!stack_ok(c, e, err)) return NULL;
    switch (e->kind) {
    case EXPR_INT:    return simple(c, TY_INT);
    case EXPR_FLOAT:  return simple(c, TY_FLOAT);
    case EXPR_STRING: return simple(c, TY_STRING);
    case EXPR_BOOL:   return simple(c, TY_BOOL);

    case EXPR_NAME: {
        if (strcmp(e->as.name.name, "_") == 0) {
            diag_set(err, e->line, e->column, "`_` cannot be read",
                     "`_` discards a value", "give it a real name if you need the value");
            return NULL;
        }
        int hops;
        Decl *d = resolve(c, e->as.name.name, &hops);
        if (d == NULL) { unknown_name(c, e->as.name.name, e->line, e->column, false, err); return NULL; }
        e->as.name.hops = hops;
        e->as.name.slot = d->slot;
        return d->type;
    }

    case EXPR_CALL:
        return check_call(c, e, used_as_value, err);

    case EXPR_UNARY: {
        Type *t = check_expr(c, e->as.unary.operand, NULL, true, err);
        if (t == NULL) return NULL;
        if (e->as.unary.op == TOK_NOT) {
            if (!need_bool_type(err, e->as.unary.operand, t, "operand of `not`")) return NULL;
            return t;
        }
        if (is_number(t)) return t;
        cannot_negate(err, e, t);
        return NULL;
    }

    case EXPR_BINARY: {
        Type *a = check_expr(c, e->as.binary.left, NULL, true, err);
        if (a == NULL) return NULL;
        Type *b = check_expr(c, e->as.binary.right, NULL, true, err);
        if (b == NULL) return NULL;
        return binary_result(c, e, e->as.binary.op, a, b, err);
    }

    case EXPR_LOGICAL: {
        const char *word = e->as.logical.op == TOK_AND ? "and" : "or";
        char what[40];
        Type *a = check_expr(c, e->as.logical.left, NULL, true, err);
        if (a == NULL) return NULL;
        snprintf(what, sizeof what, "left side of `%s`", word);
        if (!need_bool_type(err, e->as.logical.left, a, what)) return NULL;
        Type *b = check_expr(c, e->as.logical.right, NULL, true, err);
        if (b == NULL) return NULL;
        snprintf(what, sizeof what, "right side of `%s`", word);
        if (!need_bool_type(err, e->as.logical.right, b, what)) return NULL;
        return a;
    }

    case EXPR_TERNARY: {
        Type *cond = check_expr(c, e->as.ternary.cond, NULL, true, err);
        if (cond == NULL) return NULL;
        if (!need_bool_type(err, e->as.ternary.cond, cond, "the condition of `if`")) return NULL;
        Type *then = check_expr(c, e->as.ternary.then, expected, true, err);
        if (then == NULL) return NULL;
        Type *otherwise = check_expr(c, e->as.ternary.otherwise, expected, true, err);
        if (otherwise == NULL) return NULL;
        if (type_equal(then, otherwise)) return then;
        if (is_number(then) && is_number(otherwise)) {
            Type *fl = simple(c, TY_FLOAT);
            widen_if_needed(&e->as.ternary.then, fl, then);
            widen_if_needed(&e->as.ternary.otherwise, fl, otherwise);
            return fl;
        }
        branches_differ(err, e->as.ternary.otherwise, then, otherwise);
        return NULL;
    }

    case EXPR_RANGE: {
        Type *a = check_expr(c, e->as.range.start, NULL, true, err);
        if (a == NULL) return NULL;
        Type *b = check_expr(c, e->as.range.end, NULL, true, err);
        if (b == NULL) return NULL;
        if (a->tag != TY_INT || b->tag != TY_INT) {
            bool start_bad = a->tag != TY_INT;
            range_end_not_int(err, start_bad ? e->as.range.start : e->as.range.end, start_bad ? a : b);
            return NULL;
        }
        return simple(c, TY_RANGE);
    }

    case EXPR_LAMBDA:
        return check_lambda(c, e->as.lambda.proto, expected, err);
    case EXPR_WIDEN:            /* only the checker itself makes these, after checking */
        return check_expr(c, e->as.widen.operand, expected, used_as_value, err) == NULL ? NULL : simple(c, TY_FLOAT);
    }
    return NULL;
}

/* ---- functions -------------------------------------------------------- */

static bool check_stmt(Checker *c, Stmt *s, Diag *err);
static bool declare_functions(Checker *c, StmtList *list, Diag *err);
static bool check_list(Checker *c, StmtList *list, Diag *err);

/* Declares the parameters of `f` in the current scope (their types are
 * `params`), checking each default value first, so that a default may
 * only use the parameters to its left. */
static bool declare_params(Checker *c, FnProto *f, Type **params, const char *fn_name, Diag *err) {
    for (size_t i = 0; i < f->param_count; i++) {
        Param *p = &f->params[i];
        if (p->default_value != NULL) {
            Type *t = check_expr(c, p->default_value, params[i], true, err);
            if (t == NULL) return false;
            if (!type_accepts(params[i], t)) {
                char want[120], help[200];
                type_name(params[i], want, sizeof want);
                snprintf(help, sizeof help, "parameter `%s` of `%s` is %s", p->name, fn_name, want);
                return mismatch(err, p->default_value->line, p->default_value->column, params[i], t,
                                "default value here", help);
            }
            widen_if_needed(&p->default_value, params[i], t);
        }
        if (strcmp(p->name, "_") == 0) c->scope->slot_count++;      /* a slot, but no name */
        else scope_declare(c, c->scope, p->name, DECL_PARAM, false, p->line, params[i]);
    }
    return true;
}

/* The body of a declared fn: one scope holding the parameters and the
 * locals declared directly in the body; `signature` came from the
 * declaration pass. */
static bool check_function(Checker *c, FnProto *f, const Type *signature, Diag *err) {
    FnCtx ctx;
    ctx.proto = f;
    ctx.result = signature->result;
    ctx.loop_depth = 0;
    ctx.outer = c->fn;
    c->fn = &ctx;
    push_scope(c);
    bool ok = declare_params(c, f, signature->params, f->name, err);
    if (ok) ok = declare_functions(c, f->body, err) && check_list(c, f->body, err);
    f->slot_count = pop_scope(c);
    f->body->slot_count = f->slot_count;
    c->fn = ctx.outer;
    return ok;
}

/* A lambda: parameter types come from annotations or from the function
 * type the context expects (`apply(fn(x) => x + 1, 9)` learns `x: int`
 * from `apply`'s signature). The result is the annotation, the expected
 * result, or the body's type. A lambda without `-> T` whose body is a call
 * passes on whatever that call returns, possibly nothing. */
static Type *check_lambda(Checker *c, FnProto *f, const Type *expected, Diag *err) {
    const Type *shape = (expected != NULL && expected->tag == TY_FN) ? expected : NULL;
    if (shape != NULL && shape->param_count != f->param_count) {
        char want[120], summary[260], label[120];
        type_name(shape, want, sizeof want);
        snprintf(summary, sizeof summary, "expected %s, found a lambda with %zu parameter%s",
                 want, f->param_count, f->param_count == 1 ? "" : "s");
        snprintf(label, sizeof label, "lambda here");
        diag_set(err, f->line, f->column, summary, label, "give the lambda as many parameters as the function type");
        return NULL;
    }
    Type **params = f->param_count > 0 ? xmalloc(f->param_count * sizeof(Type *)) : NULL;
    for (size_t i = 0; i < f->param_count; i++) {
        Param *p = &f->params[i];
        if (p->type != NULL) {
            params[i] = type_from_expr(&c->types, p->type, err);
            if (params[i] == NULL) { free(params); return NULL; }
            if (shape != NULL && !type_equal(shape->params[i], params[i])) {
                char label[160];
                snprintf(label, sizeof label, "parameter `%s` here", p->name);
                mismatch(err, p->line, p->column, shape->params[i], params[i], label,
                         "the lambda is used where a function with that parameter type is expected");
                free(params);
                return NULL;
            }
        } else if (shape != NULL) {
            params[i] = shape->params[i];
        } else {
            char summary[160], help[200];
            snprintf(summary, sizeof summary, "parameter `%s` needs a type", p->name);
            snprintf(help, sizeof help, "write `fn(%s: int) => ...`, or pass the lambda where a "
                     "`fn(int) -> T` is expected and the type follows", p->name);
            diag_set(err, p->line, p->column, summary, "nothing says what type this is", help);
            free(params);
            return NULL;
        }
    }
    Type *annotated = NULL;
    if (f->result != NULL) {
        annotated = type_from_expr(&c->types, f->result, err);
        if (annotated == NULL) { free(params); return NULL; }
    }
    const Type *wanted = annotated != NULL ? annotated : (shape != NULL ? shape->result : NULL);
    bool forwards_call = (annotated == NULL && f->expr_body->kind == EXPR_CALL);
    bool want_value = annotated != NULL || (shape != NULL && shape->result != NULL) || !forwards_call;

    FnCtx ctx;
    ctx.proto = f;
    ctx.result = annotated;
    ctx.loop_depth = 0;
    ctx.outer = c->fn;
    c->fn = &ctx;
    push_scope(c);
    Type *result = NULL;
    bool ok = declare_params(c, f, params, "the lambda", err);
    if (ok) {
        Type *body = check_expr(c, f->expr_body, wanted, want_value, err);
        ok = body != NULL;
        if (ok && wanted != NULL && !type_accepts(wanted, body)) {
            ok = mismatch(err, f->expr_body->line, f->expr_body->column, wanted, body, "the lambda's body is this type",
                          annotated != NULL ? "the lambda declares another result type after `->`"
                                            : "the context expects a function with another result type");
        }
        if (ok) {
            if (wanted != NULL) widen_if_needed(&f->expr_body, wanted, body);
            if (wanted != NULL) result = (Type *)wanted;
            else if (body->tag != TY_NONE) result = body;
            if (shape != NULL && shape->result == NULL) result = NULL;   /* the context drops the value */
        }
    }
    f->slot_count = pop_scope(c);
    c->fn = ctx.outer;
    if (!ok) { free(params); return NULL; }
    return type_fn(&c->types, params, f->param_count, result);
}

/* Functions are visible in their whole scope, so every `fn` of a statement
 * list is declared before the list is checked (and, at run time, created
 * before the list runs). The signature is built here, so recursion and
 * mutual recursion type-check. */
static bool declare_functions(Checker *c, StmtList *list, Diag *err) {
    for (size_t i = 0; i < list->count; i++) {
        Stmt *s = list->items[i];
        if (s->kind != STMT_FN) continue;
        FnProto *f = s->as.fn.proto;
        Decl *here = scope_find_here(c->scope, f->name);
        if (here != NULL && !(c->repl_mode && c->scope->is_global)) {
            return already_declared(here, f->name, s->line, s->column, err);
        }
        Type *signature = signature_of(c, f, err);
        if (signature == NULL) return false;
        Decl *d = scope_declare(c, c->scope, f->name, DECL_FN, false, s->line, signature);
        d->proto = f;
        s->as.fn.slot = d->slot;
    }
    return true;
}

/* ---- statements ------------------------------------------------------- */

static bool check_block(Checker *c, StmtList *block, Diag *err);

static bool check_condition(Checker *c, Expr *cond, const char *what, Diag *err) {
    Type *t = check_expr(c, cond, NULL, true, err);
    return t != NULL && need_bool_type(err, cond, t, what);
}

static bool check_stmt(Checker *c, Stmt *s, Diag *err) {
    switch (s->kind) {
    case STMT_EXPR:
        return check_expr(c, s->as.expr, NULL, false, err) != NULL;

    case STMT_LET: {
        /* The value is checked first: `let x = x` must not see the new x. */
        Type *declared = NULL;
        if (s->as.let.type != NULL) {
            declared = type_from_expr(&c->types, s->as.let.type, err);
            if (declared == NULL) return false;
        }
        Type *value = check_expr(c, s->as.let.value, declared, true, err);
        if (value == NULL) return false;
        if (declared != NULL && !type_accepts(declared, value)) {
            char have[120], label[160];
            type_name(value, have, sizeof have);
            snprintf(label, sizeof label, "this value is %s", have);
            return mismatch(err, s->as.let.value->line, s->as.let.value->column, declared, value, label,
                            "change the annotation or the value");
        }
        if (declared != NULL) widen_if_needed(&s->as.let.value, declared, value);
        Type *type = declared != NULL ? declared : value;
        if (strcmp(s->as.let.name, "_") == 0) {
            s->as.let.slot = SCOPE_GLOBAL;     /* `_` is never stored */
            return true;
        }
        Decl *here = scope_find_here(c->scope, s->as.let.name);
        if (here != NULL && !(c->repl_mode && c->scope->is_global)) {
            return already_declared(here, s->as.let.name, s->line, s->column, err);
        }
        Decl *d = scope_declare(c, c->scope, s->as.let.name, DECL_VAR, s->as.let.is_mutable, s->line, type);
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
        Type *value = check_expr(c, s->as.assign.value, d->type, true, err);
        if (value == NULL) return false;
        Type *stored = value;
        if (s->as.assign.op != TOK_EQUAL) {
            TokenKind op = s->as.assign.op == TOK_PLUS_EQUAL ? TOK_PLUS
                         : s->as.assign.op == TOK_MINUS_EQUAL ? TOK_MINUS
                         : s->as.assign.op == TOK_STAR_EQUAL ? TOK_STAR
                         : s->as.assign.op == TOK_SLASH_EQUAL ? TOK_SLASH : TOK_SLASH_SLASH;
            stored = binary_result(c, s->as.assign.value, op, d->type, value, err);
            if (stored == NULL) return false;
        }
        if (!type_accepts(d->type, stored)) {
            char want[120], label[160];
            type_name(d->type, want, sizeof want);
            const char *help = "a variable keeps the type of its first value; declare a new one for another type";
            if (s->as.assign.op == TOK_SLASH_EQUAL) {
                snprintf(label, sizeof label, "`%s` is %s, but `/` gives float", s->as.assign.name, want);
                help = "use `//=` for integer division";
            } else {
                snprintf(label, sizeof label, "`%s` is %s", s->as.assign.name, want);
            }
            return mismatch(err, s->as.assign.value->line, s->as.assign.value->column, d->type, stored, label, help);
        }
        if (s->as.assign.op == TOK_EQUAL) widen_if_needed(&s->as.assign.value, d->type, stored);
        return true;
    }

    case STMT_IF:
        for (size_t i = 0; i < s->as.if_.count; i++) {
            if (!check_condition(c, s->as.if_.conds[i], "the condition of `if`", err)) return false;
            if (!check_block(c, s->as.if_.blocks[i], err)) return false;
        }
        if (s->as.if_.otherwise != NULL && !check_block(c, s->as.if_.otherwise, err)) return false;
        return true;

    case STMT_WHILE: {
        if (!check_condition(c, s->as.while_.cond, "the condition of `while`", err)) return false;
        c->fn->loop_depth++;
        bool ok = check_block(c, s->as.while_.body, err);
        c->fn->loop_depth--;
        return ok;
    }

    case STMT_FOR: {
        Type *iterable = check_expr(c, s->as.for_.iterable, NULL, true, err);
        if (iterable == NULL) return false;
        if (iterable->tag != TY_RANGE) {
            char have[120], summary[160];
            type_name(iterable, have, sizeof have);
            snprintf(summary, sizeof summary, "cannot iterate over %s", have);
            diag_set(err, s->as.for_.iterable->line, s->as.for_.iterable->column, summary,
                     "`for` needs a range here", "write a range: `for i in 0..10:`");
            return false;
        }
        /* The loop variable is slot 0 of the body's scope, a fresh `let`
         * on every iteration; the body's own statements follow it. */
        push_scope(c);
        if (strcmp(s->as.for_.name, "_") == 0) {
            c->scope->slot_count++;                    /* reserve slot 0, declare nothing */
        } else {
            scope_declare(c, c->scope, s->as.for_.name, DECL_VAR, false, s->as.for_.name_line, simple(c, TY_INT));
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
        if (s->as.return_.value != NULL && c->fn->result == NULL) {
            char summary[160];
            snprintf(summary, sizeof summary, "`%s` returns nothing", name);
            diag_set(err, s->line, s->column, summary, "but a value is returned here",
                     "add `-> type` after the parameters, or write a bare `return`");
            return false;
        }
        if (s->as.return_.value == NULL && c->fn->result != NULL) {
            char summary[160];
            snprintf(summary, sizeof summary, "`%s` must return a value", name);
            diag_set(err, s->line, s->column, summary, "`return` without a value here",
                     "write `return value`");
            return false;
        }
        if (s->as.return_.value == NULL) return true;
        Type *t = check_expr(c, s->as.return_.value, c->fn->result, true, err);
        if (t == NULL) return false;
        if (type_accepts(c->fn->result, t)) {
            widen_if_needed(&s->as.return_.value, c->fn->result, t);
            return true;
        }
        char want[120], help[200];
        type_name(c->fn->result, want, sizeof want);
        snprintf(help, sizeof help, "`%s` is declared to return %s", name, want);
        return mismatch(err, s->as.return_.value->line, s->as.return_.value->column, c->fn->result, t,
                        "returned here", help);
    }

    case STMT_FN: {
        /* Declared by declare_functions; only the body is left to check. */
        int hops;
        const Decl *d = resolve(c, s->as.fn.proto->name, &hops);
        return check_function(c, s->as.fn.proto, d->type, err);
    }
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
    top.result = NULL;
    top.loop_depth = 0;
    top.outer = NULL;
    char base;
    c->stack_base = &base;
    c->fn = &top;
    c->top = &prog->body;
    Scope *global = c->scope;
    if (c->repl_mode) {
        /* A failed input must leave the session as it was: remember what
         * this input adds to or changes in the global scope. */
        c->undo_count = 0;
        if (c->undo == NULL) c->undo = xmalloc(4 * sizeof(Undo)), c->undo_capacity = 4;
        c->global_count_before = global->count;
    }
    bool ok = declare_functions(c, &prog->body, err) && check_list(c, &prog->body, err);
    if (c->repl_mode && !ok) {
        for (size_t i = c->undo_count; i > 0; i--) {
            global->items[c->undo[i - 1].index] = c->undo[i - 1].old;
        }
        for (size_t i = c->global_count_before; i < global->count; i++) free(global->items[i].name);
        global->count = c->global_count_before;
    }
    c->fn = NULL;
    c->top = NULL;
    return ok;
}
