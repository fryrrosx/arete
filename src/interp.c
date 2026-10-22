/* interp.c - tree-walking interpreter. Values are counted (arete_rt.h);
 * every `eval` hands the caller one reference, which the caller releases.
 * Scopes are ObjEnv chains: the resolver (check.c) numbered local names as
 * (hops, slot), only the global scope is searched by name. Statements
 * return a Flow so that `break`, `continue` and `return` unwind the walk
 * without C longjmp. */
#include "interp.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arete_rt.h"
#include "env.h"
#include "rt_sys.h"
#include "util.h"

/* Nested calls allowed before `too many nested calls` (SPEC.md section 2.5). */
#define MAX_CALL_DEPTH 1000

/* The walk stops with `stack overflow` instead of crashing when it has
 * used all but this much of the stack rt_sys reports (8 MB on every
 * platform the project builds on; rt_sys.c sets that for MSVC). */
#define STACK_MARGIN (1u * 1024u * 1024u)

enum { NATIVE_NONE = 0, NATIVE_PRINT = 1 };

typedef enum { FLOW_NORMAL, FLOW_BREAK, FLOW_CONTINUE, FLOW_RETURN, FLOW_ERROR } Flow;

struct Interp {
    ObjEnv *globals;
    ObjEnv *env;             /* innermost scope while running */
    Diag *err;               /* where a runtime error is recorded */
    const Source *source;    /* the source of the code running now; NULL for the program's own */
    int depth;               /* nested calls */
    Value return_value;      /* set by `return`, taken by the call */
    const char *stack_base;  /* address of a local in interp_run, for the stack guard */
    size_t stack_budget;     /* bytes the walk may use below stack_base */
};

Interp *interp_new(void) {
    Interp *in = xcalloc(sizeof *in);
    size_t limit = rt_stack_limit();
    in->stack_budget = limit > 2 * STACK_MARGIN ? limit - STACK_MARGIN : limit / 2;
    in->globals = env_new(NULL, 0);
    in->env = in->globals;
    in->return_value = value_null();
    env_define_global(in->globals, "print", value_function("print", NULL, NULL, NATIVE_PRINT), false);
    return in;
}

void interp_free(Interp *in) {
    if (in == NULL) return;
    value_release(in->return_value);
    env_release(in->globals);
    rt_shutdown();           /* what closures kept alive in cycles */
    free(in);
}

/* ---- runtime errors --------------------------------------------------- */

static bool fail_at(Interp *in, int line, int column, const char *summary,
                    const char *label, const char *help) {
    diag_set(in->err, line, column, summary, label, help);
    in->err->source = in->source;
    return false;
}

static bool fail(Interp *in, const Expr *at, const char *summary,
                 const char *label, const char *help) {
    return fail_at(in, at->line, at->column, summary, label, help);
}

static bool fail_operands(Interp *in, const Expr *at, TokenKind op, Value a, Value b) {
    char summary[160];
    snprintf(summary, sizeof summary, "cannot apply `%s` to %s and %s",
             op_symbol(op), value_type_name(a), value_type_name(b));
    const char *help = NULL;
    bool a_str = value_is(a, OBJ_STRING), b_str = value_is(b, OBJ_STRING);
    if (a_str != b_str && op == TOK_PLUS) {
        help = "both sides of `+` must be strings or both numbers";
    }
    return fail(in, at, summary, "here", help);
}

/* The C stack is finite; the call-depth and expression-depth limits keep
 * normal programs far from it, this guard turns the rest into an error.
 * A distance far beyond any stack means the locals are not on it (a
 * sanitizer's fake stack); then there is nothing to measure. */
static bool stack_ok(Interp *in, const Expr *at) {
    char here;
    size_t used = (size_t)(in->stack_base > &here ? in->stack_base - &here : &here - in->stack_base);
    if (used < in->stack_budget || used > 64 * in->stack_budget) return true;
    return fail(in, at, "stack overflow", "the program is nested too deeply here",
                "reduce the nesting of calls and expressions");
}

/* Message helpers kept out of the hot functions, so that their buffers do
 * not sit in every frame of the recursive walk. */
static bool fail_no_value(Interp *in, const Expr *e) {
    char summary[160], help[160];
    snprintf(summary, sizeof summary, "`%s` has no value yet", e->as.name.name);
    snprintf(help, sizeof help, "declare `%s` before the code that reads it", e->as.name.name);
    return fail(in, e, summary, "its declaration has not run yet, or failed", help);
}

static bool fail_returns_nothing(Interp *in, const Expr *call, const char *name) {
    char summary[160];
    snprintf(summary, sizeof summary, "`%s` returns nothing and cannot be used as a value", name);
    return fail(in, call, summary, "used as a value here", "call it on its own line");
}

static bool fail_not_callable(Interp *in, const Expr *e, Value callee) {
    char summary[160], label[120];
    if (e->as.call.callee->kind == EXPR_NAME) {
        snprintf(summary, sizeof summary, "`%s` is not a function", e->as.call.callee->as.name.name);
        snprintf(label, sizeof label, "it holds %s", value_type_name(callee));
    } else {
        snprintf(summary, sizeof summary, "cannot call %s", value_type_name(callee));
        snprintf(label, sizeof label, "this is %s, not a function", value_type_name(callee));
    }
    return fail(in, e, summary, label, NULL);
}

static bool fail_too_deep(Interp *in, const Expr *call) {
    char label[80];
    snprintf(label, sizeof label, "this would be call number %d, one inside the other", MAX_CALL_DEPTH + 1);
    return fail(in, call, "too many nested calls", label,
                "the limit is 1000 nested calls; does the recursion reach its base case?");
}

static bool fail_no_return(Interp *in, const Expr *call, const FnProto *proto) {
    char summary[160];
    snprintf(summary, sizeof summary, "`%s` ended without returning a value", proto->name);
    return fail(in, call, summary, "called here", "every path through the function must `return` a value");
}

/* ---- arithmetic ------------------------------------------------------- */

static bool is_number(Value v) {
    return v.kind == VAL_INT || v.kind == VAL_FLOAT;
}

static double as_double(Value v) {
    return v.kind == VAL_INT ? (double)v.as.integer : v.as.number;
}

/* Checked 64-bit arithmetic without compiler extensions, so that it builds
 * the same way with gcc, clang and MSVC. */
static bool add_checked(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return false;
    *out = a + b;
    return true;
}

static bool sub_checked(int64_t a, int64_t b, int64_t *out) {
    if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) return false;
    *out = a - b;
    return true;
}

static bool mul_checked(int64_t a, int64_t b, int64_t *out) {
    if (a == 0 || b == 0) { *out = 0; return true; }
    if (a == -1) { if (b == INT64_MIN) return false; *out = -b; return true; }
    if (b == -1) { if (a == INT64_MIN) return false; *out = -a; return true; }
    if (a > 0 ? (b > 0 ? a > INT64_MAX / b : b < INT64_MIN / a)
              : (b > 0 ? a < INT64_MIN / b : a < INT64_MAX / b)) return false;
    *out = a * b;
    return true;
}

/* Floor division with Python's sign rules (the remainder, when Karim adds
 * `%`, must satisfy a == (a // b) * b + a % b with the sign of b). */
static int64_t floor_div(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
    return q;
}

/* Float floor division the way CPython does it, so that it stays
 * consistent with a float `%` built on fmod: 1.0 // 0.1 is 9.0, not 10.0. */
static double float_floor_div(double x, double y) {
    double mod = fmod(x, y);
    double div = (x - mod) / y;
    if (mod != 0.0 && ((y < 0) != (mod < 0))) div -= 1.0;
    if (div == 0.0) return copysign(0.0, x / y);
    double q = floor(div);
    if (div - q > 0.5) q += 1.0;
    return q;
}

static bool pow_checked(int64_t base, int64_t exp, int64_t *out) {
    int64_t result = 1;
    while (exp > 0) {
        if (exp & 1) { if (!mul_checked(result, base, &result)) return false; }
        exp >>= 1;
        if (exp > 0 && !mul_checked(base, base, &base)) return false;
    }
    *out = result;
    return true;
}

static bool arith(Interp *in, const Expr *at, TokenKind op, Value a, Value b, Value *out) {
    if (op == TOK_PLUS && value_is(a, OBJ_STRING) && value_is(b, OBJ_STRING)) {
        *out = value_string_concat(a, b);
        return true;
    }
    if (!is_number(a) || !is_number(b)) return fail_operands(in, at, op, a, b);

    bool both_int = (a.kind == VAL_INT && b.kind == VAL_INT);
    char summary[120];

    if (op == TOK_SLASH) {                              /* always a float */
        double d = as_double(b);
        if (d == 0.0) return fail(in, at, "division by zero", "the divisor is zero here", NULL);
        *out = value_float(as_double(a) / d);
        return true;
    }
    if (both_int) {
        int64_t x = a.as.integer, y = b.as.integer, r = 0;
        bool ok = true;
        switch (op) {
        case TOK_MINUS: ok = sub_checked(x, y, &r); break;
        case TOK_STAR:  ok = mul_checked(x, y, &r); break;
        case TOK_PLUS:  ok = add_checked(x, y, &r); break;
        case TOK_SLASH_SLASH:
            if (y == 0) return fail(in, at, "division by zero", "the divisor is zero here", NULL);
            if (x == INT64_MIN && y == -1) { ok = false; break; }
            r = floor_div(x, y);
            break;
        case TOK_STAR_STAR:
            if (y < 0) {
                if (x == 0) return fail(in, at, "division by zero", "0 raised to a negative power", NULL);
                *out = value_float(pow((double)x, (double)y));   /* 2 ** -1 is 0.5 */
                return true;
            }
            ok = pow_checked(x, y, &r);
            break;
        default:
            return fail_operands(in, at, op, a, b);
        }
        if (!ok) {
            snprintf(summary, sizeof summary, "integer overflow in `%s`", op_symbol(op));
            return fail(in, at, summary, "the result does not fit in 64 bits", NULL);
        }
        *out = value_int(r);
        return true;
    }
    double x = as_double(a), y = as_double(b);
    switch (op) {
    case TOK_PLUS:  *out = value_float(x + y); return true;
    case TOK_MINUS: *out = value_float(x - y); return true;
    case TOK_STAR:  *out = value_float(x * y); return true;
    case TOK_SLASH_SLASH:
        if (y == 0.0) return fail(in, at, "division by zero", "the divisor is zero here", NULL);
        *out = value_float(float_floor_div(x, y));
        return true;
    case TOK_STAR_STAR:
        if (x == 0.0 && y < 0) return fail(in, at, "division by zero", "0 raised to a negative power", NULL);
        *out = value_float(pow(x, y));
        return true;
    default:
        return fail_operands(in, at, op, a, b);
    }
}

/* ---- comparison ------------------------------------------------------- */

/* Compares an int with a float exactly, without rounding the int to a
 * double first (ints above 2^53 would collide). False for NaN. */
static bool compare_int_float(int64_t i, double f, int *cmp) {
    if (isnan(f)) return false;
    if (f >= 9223372036854775808.0) { *cmp = -1; return true; }    /* f > every int */
    if (f < -9223372036854775808.0) { *cmp = 1; return true; }
    double whole = floor(f);
    int64_t w = (int64_t)whole;
    if (i < w) *cmp = -1;
    else if (i > w) *cmp = 1;
    else *cmp = (f > whole) ? -1 : 0;        /* same whole part: the fraction decides */
    return true;
}

/* Orders two numbers: -1, 0 or 1 in *cmp. False when one is NaN. */
static bool compare_numbers(Value a, Value b, int *cmp) {
    if (a.kind == VAL_INT && b.kind == VAL_INT) {
        *cmp = (a.as.integer > b.as.integer) - (a.as.integer < b.as.integer);
        return true;
    }
    if (a.kind == VAL_INT) return compare_int_float(a.as.integer, b.as.number, cmp);
    if (b.kind == VAL_INT) {
        if (!compare_int_float(b.as.integer, a.as.number, cmp)) return false;
        *cmp = -*cmp;
        return true;
    }
    if (isnan(a.as.number) || isnan(b.as.number)) return false;
    *cmp = (a.as.number > b.as.number) - (a.as.number < b.as.number);
    return true;
}

static bool values_equal(Value a, Value b) {
    if (is_number(a) && is_number(b)) {
        int cmp;
        return compare_numbers(a, b, &cmp) && cmp == 0;    /* 1 == 1.0 */
    }
    if (a.kind != b.kind) return false;
    switch (a.kind) {
    case VAL_NULL: return true;
    case VAL_BOOL: return a.as.boolean == b.as.boolean;
    case VAL_OBJ:
        if (a.as.obj->kind != b.as.obj->kind) return false;
        switch (a.as.obj->kind) {
        case OBJ_STRING: {
            ObjString *x = (ObjString *)a.as.obj, *y = (ObjString *)b.as.obj;
            return x->length == y->length && memcmp(x->chars, y->chars, x->length) == 0;
        }
        case OBJ_RANGE: {
            /* Equal when they hold the same ints: all empty ranges are
             * equal, 0..3 == 0..=2. */
            ObjRange *x = (ObjRange *)a.as.obj, *y = (ObjRange *)b.as.obj;
            bool x_empty = x->inclusive ? x->start > x->end : x->start >= x->end;
            bool y_empty = y->inclusive ? y->start > y->end : y->start >= y->end;
            if (x_empty || y_empty) return x_empty && y_empty;
            int64_t x_last = x->inclusive ? x->end : x->end - 1;
            int64_t y_last = y->inclusive ? y->end : y->end - 1;
            return x->start == y->start && x_last == y_last;
        }
        default:
            return a.as.obj == b.as.obj;         /* functions: the same function */
        }
    default: return false;
    }
}

/* Orders two numbers or two strings. False when they cannot be ordered. */
static bool order(Value a, Value b, int *cmp) {
    if (is_number(a) && is_number(b)) {
        if (compare_numbers(a, b, cmp)) return true;
        *cmp = 2;                                  /* NaN: every ordering is false */
        return true;
    }
    if (value_is(a, OBJ_STRING) && value_is(b, OBJ_STRING)) {
        ObjString *x = (ObjString *)a.as.obj, *y = (ObjString *)b.as.obj;
        size_t n = x->length < y->length ? x->length : y->length;
        int c = memcmp(x->chars, y->chars, n);
        if (c == 0) c = (x->length > y->length) - (x->length < y->length);
        *cmp = c;
        return true;
    }
    return false;
}

static bool compare(Interp *in, const Expr *at, TokenKind op, Value a, Value b, Value *out) {
    if (op == TOK_EQUAL_EQUAL) { *out = value_bool(values_equal(a, b)); return true; }
    if (op == TOK_BANG_EQUAL)  { *out = value_bool(!values_equal(a, b)); return true; }
    int cmp = 0;
    if (!order(a, b, &cmp)) return fail_operands(in, at, op, a, b);
    bool r = false;
    switch (op) {
    case TOK_LESS: r = cmp == -1; break;
    case TOK_LESS_EQUAL: r = cmp == -1 || cmp == 0; break;
    case TOK_GREATER: r = cmp == 1; break;
    case TOK_GREATER_EQUAL: r = cmp == 1 || cmp == 0; break;
    default: break;
    }
    *out = value_bool(r);
    return true;
}

/* ---- evaluation ------------------------------------------------------- */

static bool eval(Interp *in, const Expr *e, Value *out);
static bool eval_call(Interp *in, const Expr *e, Value *out, bool want_value);
static Flow exec_list(Interp *in, const StmtList *list, ObjEnv *env);

static bool need_bool(Interp *in, const Expr *at, Value v, const char *what) {
    if (v.kind == VAL_BOOL) return true;
    char summary[120], help[120];
    snprintf(summary, sizeof summary, "%s must be bool, found %s", what, value_type_name(v));
    snprintf(help, sizeof help, "compare explicitly, for example `x != 0`");
    value_release(v);
    return fail(in, at, summary, "here", help);
}

static bool eval_binary(Interp *in, const Expr *e, Value *out) {
    Value a, b;
    if (!eval(in, e->as.binary.left, &a)) return false;
    if (!eval(in, e->as.binary.right, &b)) {
        value_release(a);
        return false;
    }
    bool ok;
    switch (e->as.binary.op) {
    case TOK_EQUAL_EQUAL: case TOK_BANG_EQUAL:
    case TOK_LESS: case TOK_LESS_EQUAL: case TOK_GREATER: case TOK_GREATER_EQUAL:
        ok = compare(in, e, e->as.binary.op, a, b, out);
        break;
    default:
        ok = arith(in, e, e->as.binary.op, a, b, out);
        break;
    }
    value_release(a);
    value_release(b);
    return ok;
}

static bool eval_logical(Interp *in, const Expr *e, Value *out) {
    Value a;
    if (!eval(in, e->as.logical.left, &a)) return false;
    if (!need_bool(in, e->as.logical.left, a, e->as.logical.op == TOK_AND ? "left side of `and`"
                                                                          : "left side of `or`")) return false;
    bool short_circuit = (e->as.logical.op == TOK_AND) ? !a.as.boolean : a.as.boolean;
    if (short_circuit) {
        *out = a;
        return true;
    }
    Value b;
    if (!eval(in, e->as.logical.right, &b)) return false;
    if (!need_bool(in, e->as.logical.right, b, e->as.logical.op == TOK_AND ? "right side of `and`"
                                                                           : "right side of `or`")) return false;
    *out = b;
    return true;
}

static bool eval_unary(Interp *in, const Expr *e, Value *out) {
    Value v;
    if (!eval(in, e->as.unary.operand, &v)) return false;
    if (e->as.unary.op == TOK_NOT) {
        if (!need_bool(in, e->as.unary.operand, v, "operand of `not`")) return false;
        *out = value_bool(!v.as.boolean);
        return true;
    }
    if (v.kind == VAL_INT) {
        if (v.as.integer == INT64_MIN) {
            return fail(in, e, "integer overflow in `-`", "the result does not fit in 64 bits", NULL);
        }
        *out = value_int(-v.as.integer);
        return true;
    }
    if (v.kind == VAL_FLOAT) {
        *out = value_float(-v.as.number);
        return true;
    }
    char summary[120];
    snprintf(summary, sizeof summary, "cannot negate %s", value_type_name(v));
    value_release(v);
    return fail(in, e, summary, "here", NULL);
}

static bool eval_range(Interp *in, const Expr *e, Value *out) {
    Value a, b;
    if (!eval(in, e->as.range.start, &a)) return false;
    if (!eval(in, e->as.range.end, &b)) {
        value_release(a);
        return false;
    }
    bool ok = true;
    if (a.kind != VAL_INT || b.kind != VAL_INT) {
        char summary[160];
        const Expr *bad = a.kind != VAL_INT ? e->as.range.start : e->as.range.end;
        snprintf(summary, sizeof summary, "range ends must be int, found %s",
                 value_type_name(a.kind != VAL_INT ? a : b));
        ok = fail(in, bad, summary, "this end", "a range is `start..end` over integers");
    } else {
        *out = value_range(a.as.integer, b.as.integer, e->as.range.inclusive);
    }
    value_release(a);
    value_release(b);
    return ok;
}

/* ---- calls ------------------------------------------------------------ */

static bool call_print(Interp *in, const Expr *call, Value *args, size_t count) {
    if (count != 1 || call->as.call.names[0] != NULL) {
        char label[80];
        snprintf(label, sizeof label, "called with %zu argument%s", count, count == 1 ? "" : "s");
        return fail(in, call, "print takes exactly one argument", label, "write print(x)");
    }
    value_print(args[0], stdout);
    fputc('\n', stdout);
    return true;
}

/* Binds the arguments of `call` to the parameters of `f` in `env`: the
 * same rules as check_arguments in check.c, applied here because the
 * callee may be a function value the checker could not see. Errors point
 * into the caller's source. */
static bool bind_arguments(Interp *in, const Expr *call, const FnProto *f, Value *args, ObjEnv *env) {
    const char *name = f->name != NULL ? f->name : "the lambda";
    size_t positional = 0;
    for (size_t i = 0; i < call->as.call.arg_count; i++) {
        const Expr *arg = call->as.call.args[i];
        const char *arg_name = call->as.call.names[i];
        size_t index;
        if (arg_name == NULL) {
            if (positional >= f->param_count) {
                char summary[160], label[120];
                snprintf(summary, sizeof summary, "too many arguments to `%s`", name);
                snprintf(label, sizeof label, "it takes %zu, this is number %zu",
                         f->param_count, positional + 1);
                return fail(in, arg, summary, label, NULL);
            }
            index = positional++;
        } else {
            index = f->param_count;
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
                return fail(in, arg, summary, "named argument here", help);
            }
            if (env->slots[index].kind != VAL_UNSET) {
                char summary[160];
                snprintf(summary, sizeof summary, "argument `%s` is given twice", arg_name);
                return fail(in, arg, summary, "second time here", "it was already passed by position");
            }
        }
        value_retain(args[i]);
        env->slots[index] = args[i];
    }
    for (size_t k = 0; k < f->param_count; k++) {
        if (env->slots[k].kind == VAL_UNSET && f->params[k].default_value == NULL) {
            char summary[160], help[160];
            snprintf(summary, sizeof summary, "missing argument `%s` in call to `%s`",
                     f->params[k].name, name);
            snprintf(help, sizeof help, "pass it by position or by name: %s = ...", f->params[k].name);
            return fail(in, call, summary, "call here", help);
        }
    }
    return true;
}

/* Runs `f`. A slot still VAL_UNSET after binding takes its default. When
 * the function returns nothing, `out` is left VAL_UNSET if the caller does
 * not want a value, otherwise that is the error "cannot be used as a value"
 * (the checker reports it before running when it can see the callee). */
static bool call_function(Interp *in, const Expr *call, ObjFunction *f, Value *args, size_t count, Value *out,
                          bool want_value) {
    *out = (Value){ .kind = VAL_UNSET };
    if (f->native == NATIVE_PRINT) {
        if (want_value) return fail_returns_nothing(in, call, f->name);
        return call_print(in, call, args, count);
    }
    if (in->depth >= MAX_CALL_DEPTH) return fail_too_deep(in, call);
    const FnProto *proto = f->proto;
    ObjEnv *env = env_new(f->closure, proto->slot_count);
    if (!bind_arguments(in, call, proto, args, env)) {
        env_release(env);
        return false;
    }

    ObjEnv *caller_env = in->env;
    const Source *caller_source = in->source;
    in->env = env;
    in->source = proto->source;
    in->depth++;
    bool ok = true;
    for (size_t k = 0; ok && k < proto->param_count; k++) {
        if (env->slots[k].kind == VAL_UNSET) ok = eval(in, proto->params[k].default_value, &env->slots[k]);
    }
    if (ok && proto->expr_body != NULL) {
        /* A lambda whose body is a call passes on what the call returns,
         * possibly nothing; any other body is a value. */
        if (proto->result == NULL && proto->expr_body->kind == EXPR_CALL) {
            ok = eval_call(in, proto->expr_body, out, false);
            if (ok && want_value && out->kind == VAL_UNSET) {
                in->source = caller_source;
                ok = fail_returns_nothing(in, call, f->name);
            }
        } else {
            ok = eval(in, proto->expr_body, out);
        }
    } else if (ok) {
        Flow flow = exec_list(in, proto->body, env);
        if (flow == FLOW_ERROR) {
            ok = false;
        } else if (flow == FLOW_RETURN) {
            *out = in->return_value;
            in->return_value = value_null();
        } else if (proto->result != NULL) {
            in->source = caller_source;
            ok = fail_no_return(in, call, proto);
        } else if (want_value) {
            in->source = caller_source;
            ok = fail_returns_nothing(in, call, proto->name);
        }
    }
    in->depth--;
    in->env = caller_env;
    in->source = caller_source;
    env_release(env);
    return ok;
}

static bool eval_call(Interp *in, const Expr *e, Value *out, bool want_value) {
    Value callee;
    if (!eval(in, e->as.call.callee, &callee)) return false;
    if (!value_is(callee, OBJ_FUNCTION)) {
        fail_not_callable(in, e, callee);
        value_release(callee);
        return false;
    }
    size_t count = e->as.call.arg_count;
    Value *args = count > 0 ? xmalloc(count * sizeof(Value)) : NULL;
    size_t done = 0;                          /* arguments evaluated, each holding a reference */
    while (done < count && eval(in, e->as.call.args[done], &args[done])) done++;
    bool ok = (done == count);
    if (ok) ok = call_function(in, e, (ObjFunction *)callee.as.obj, args, count, out, want_value);
    for (size_t i = 0; i < done; i++) value_release(args[i]);
    free(args);
    value_release(callee);
    return ok;
}

/* Evaluates an expression into a value the caller owns (must release). */
static bool eval(Interp *in, const Expr *e, Value *out) {
    if (!stack_ok(in, e)) return false;
    switch (e->kind) {
    case EXPR_INT:    *out = value_int(e->as.integer); return true;
    case EXPR_FLOAT:  *out = value_float(e->as.number); return true;
    case EXPR_BOOL:   *out = value_bool(e->as.boolean); return true;
    case EXPR_STRING: *out = value_string(e->as.string.chars, e->as.string.length); return true;
    case EXPR_NAME: {
        Value *slot;
        if (e->as.name.hops == SCOPE_GLOBAL) {
            Binding *b = env_find_global(in->globals, e->as.name.name);
            if (b == NULL) return fail_no_value(in, e);
            slot = &b->value;
        } else {
            slot = env_slot(in->env, e->as.name.hops, e->as.name.slot);
            if (slot->kind == VAL_UNSET) return fail_no_value(in, e);   /* its `let` has not run */
        }
        value_retain(*slot);
        *out = *slot;
        return true;
    }
    case EXPR_CALL:    return eval_call(in, e, out, true);
    case EXPR_UNARY:   return eval_unary(in, e, out);
    case EXPR_BINARY:  return eval_binary(in, e, out);
    case EXPR_LOGICAL: return eval_logical(in, e, out);
    case EXPR_TERNARY: {
        Value c;
        if (!eval(in, e->as.ternary.cond, &c)) return false;
        if (!need_bool(in, e->as.ternary.cond, c, "the condition of `if`")) return false;
        return eval(in, c.as.boolean ? e->as.ternary.then : e->as.ternary.otherwise, out);
    }
    case EXPR_RANGE:   return eval_range(in, e, out);
    case EXPR_LAMBDA:
        env_retain(in->env);
        *out = value_function("lambda", e->as.lambda.proto, in->env, NATIVE_NONE);
        return true;
    }
    *out = value_null();
    return true;
}

/* ---- statements ------------------------------------------------------- */

static TokenKind base_op(TokenKind compound) {
    switch (compound) {
    case TOK_PLUS_EQUAL: return TOK_PLUS;
    case TOK_MINUS_EQUAL: return TOK_MINUS;
    case TOK_STAR_EQUAL: return TOK_STAR;
    case TOK_SLASH_EQUAL: return TOK_SLASH;
    case TOK_SLASH_SLASH_EQUAL: return TOK_SLASH_SLASH;
    default: return TOK_EQUAL;
    }
}

/* Stores a value where the resolver said the name lives. */
static void store(Interp *in, int hops, int slot, const char *name, Value v, bool is_mutable) {
    if (hops == SCOPE_GLOBAL) {
        env_define_global(in->globals, name, v, is_mutable);
        return;
    }
    Value *target = env_slot(in->env, hops, slot);
    value_release(*target);
    *target = v;
}

/* Creates the function values a statement list declares, before the list
 * runs: functions are visible in their whole scope (phase 7 decision). */
static void define_functions(Interp *in, const StmtList *list, ObjEnv *env) {
    for (size_t i = 0; i < list->count; i++) {
        const Stmt *s = list->items[i];
        if (s->kind != STMT_FN) continue;
        const FnProto *f = s->as.fn.proto;
        env_retain(env);
        Value fv = value_function(f->name, f, env, NATIVE_NONE);
        store(in, s->as.fn.slot == SCOPE_GLOBAL ? SCOPE_GLOBAL : 0, s->as.fn.slot, f->name, fv, false);
    }
}

/* A block is a scope: its variables are released when it ends. */
static Flow exec_block(Interp *in, const StmtList *block) {
    ObjEnv *outer = in->env;
    ObjEnv *inner = env_new(outer, block->slot_count);
    Flow flow = exec_list(in, block, inner);
    in->env = outer;
    env_release(inner);
    return flow;
}

static Flow exec_for(Interp *in, const Stmt *s) {
    Value iterable;
    if (!eval(in, s->as.for_.iterable, &iterable)) return FLOW_ERROR;
    if (!value_is(iterable, OBJ_RANGE)) {
        char summary[120];
        snprintf(summary, sizeof summary, "cannot iterate over %s", value_type_name(iterable));
        value_release(iterable);
        fail(in, s->as.for_.iterable, summary, "`for` needs a range here", "write a range: `for i in 0..10:`");
        return FLOW_ERROR;
    }
    ObjRange *range = (ObjRange *)iterable.as.obj;
    ObjEnv *outer = in->env;
    Flow flow = FLOW_NORMAL;
    int64_t i = range->start;
    bool more = range->inclusive ? i <= range->end : i < range->end;
    while (more) {
        ObjEnv *iteration = env_new(outer, s->as.for_.body->slot_count);
        iteration->slots[0] = value_int(i);
        flow = exec_list(in, s->as.for_.body, iteration);
        in->env = outer;
        env_release(iteration);
        if (flow == FLOW_BREAK) { flow = FLOW_NORMAL; break; }
        if (flow == FLOW_CONTINUE) flow = FLOW_NORMAL;
        if (flow != FLOW_NORMAL) break;
        if (i == range->end) break;           /* inclusive: the end was the last value */
        i++;
        more = range->inclusive ? i <= range->end : i < range->end;
    }
    value_release(iterable);
    return flow;
}

static Flow exec_stmt(Interp *in, const Stmt *s) {
    switch (s->kind) {
    case STMT_EXPR: {
        Value v;
        bool ok = s->as.expr->kind == EXPR_CALL ? eval_call(in, s->as.expr, &v, false)   /* the value is dropped */
                                                : eval(in, s->as.expr, &v);
        if (!ok) return FLOW_ERROR;
        value_release(v);
        return FLOW_NORMAL;
    }
    case STMT_LET: {
        Value v;
        if (!eval(in, s->as.let.value, &v)) return FLOW_ERROR;
        if (strcmp(s->as.let.name, "_") == 0) {
            value_release(v);                      /* `_` discards the value */
            return FLOW_NORMAL;
        }
        store(in, s->as.let.slot == SCOPE_GLOBAL ? SCOPE_GLOBAL : 0, s->as.let.slot,
              s->as.let.name, v, s->as.let.is_mutable);
        return FLOW_NORMAL;
    }
    case STMT_ASSIGN: {
        Value v;
        if (!eval(in, s->as.assign.value, &v)) return FLOW_ERROR;
        Value *target;
        if (s->as.assign.hops == SCOPE_GLOBAL) {
            Binding *b = env_find_global(in->globals, s->as.assign.name);
            if (b == NULL) {
                value_release(v);
                fail_at(in, s->line, s->column, "assignment to a name without a value",
                        "its declaration failed earlier", NULL);
                return FLOW_ERROR;
            }
            target = &b->value;
        } else {
            target = env_slot(in->env, s->as.assign.hops, s->as.assign.slot);
        }
        if (target->kind == VAL_UNSET && s->as.assign.op != TOK_EQUAL) {
            value_release(v);
            char summary[160];
            snprintf(summary, sizeof summary, "`%s` has no value yet", s->as.assign.name);
            fail_at(in, s->line, s->column, summary, "its declaration has not run yet",
                    "declare it before the code that changes it");
            return FLOW_ERROR;
        }
        if (s->as.assign.op != TOK_EQUAL) {
            Value result;
            bool ok = arith(in, s->as.assign.value, base_op(s->as.assign.op), *target, v, &result);
            value_release(v);
            if (!ok) return FLOW_ERROR;
            v = result;
        }
        value_release(*target);
        *target = v;
        return FLOW_NORMAL;
    }
    case STMT_IF: {
        for (size_t i = 0; i < s->as.if_.count; i++) {
            Value c;
            if (!eval(in, s->as.if_.conds[i], &c)) return FLOW_ERROR;
            if (!need_bool(in, s->as.if_.conds[i], c, "the condition of `if`")) return FLOW_ERROR;
            if (c.as.boolean) return exec_block(in, s->as.if_.blocks[i]);
        }
        if (s->as.if_.otherwise != NULL) return exec_block(in, s->as.if_.otherwise);
        return FLOW_NORMAL;
    }
    case STMT_WHILE:
        for (;;) {
            Value c;
            if (!eval(in, s->as.while_.cond, &c)) return FLOW_ERROR;
            if (!need_bool(in, s->as.while_.cond, c, "the condition of `while`")) return FLOW_ERROR;
            if (!c.as.boolean) return FLOW_NORMAL;
            Flow flow = exec_block(in, s->as.while_.body);
            if (flow == FLOW_BREAK) return FLOW_NORMAL;
            if (flow == FLOW_CONTINUE || flow == FLOW_NORMAL) continue;
            return flow;
        }
    case STMT_FOR:
        return exec_for(in, s);
    case STMT_BREAK:
        return FLOW_BREAK;
    case STMT_CONTINUE:
        return FLOW_CONTINUE;
    case STMT_RETURN: {
        Value v = value_null();
        if (s->as.return_.value != NULL && !eval(in, s->as.return_.value, &v)) return FLOW_ERROR;
        value_release(in->return_value);
        in->return_value = v;
        return FLOW_RETURN;
    }
    case STMT_FN:
        return FLOW_NORMAL;      /* created by define_functions when the scope began */
    }
    return FLOW_NORMAL;
}

/* Runs the statements of a list in `env`, functions first. The caller
 * restores in->env afterwards. */
static Flow exec_list(Interp *in, const StmtList *list, ObjEnv *env) {
    in->env = env;
    define_functions(in, list, env);
    for (size_t i = 0; i < list->count; i++) {
        Flow flow = exec_stmt(in, list->items[i]);
        if (flow != FLOW_NORMAL) return flow;
    }
    return FLOW_NORMAL;
}

int interp_run(Interp *in, const Program *prog, Diag *err) {
    char base;
    in->stack_base = &base;
    in->err = err;
    in->source = NULL;
    in->depth = 0;
    Flow flow = exec_list(in, &prog->body, in->globals);
    in->env = in->globals;
    if (flow == FLOW_ERROR) {
        fflush(stdout);
        return 1;
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fputs("error: cannot write output\n", stderr);
        return 1;
    }
    return 0;
}
