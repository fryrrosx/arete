#include "interp.h"

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arete_rt.h"
#include "env.h"
#include "util.h"

struct Interp {
    Env *globals;
    Env *env;        /* innermost scope while running */
    Diag *err;       /* where a runtime error is recorded */
};

Interp *interp_new(void) {
    Interp *in = xcalloc(sizeof *in);
    in->globals = env_new(NULL);
    in->env = in->globals;
    return in;
}

void interp_free(Interp *in) {
    if (in == NULL) return;
    env_free(in->globals);
    free(in);
}

/* ---- runtime errors --------------------------------------------------- */

static bool fail(Interp *in, const Expr *at, const char *summary,
                 const char *label, const char *help) {
    diag_set(in->err, at->line, at->column, summary, label, help);
    return false;
}

static const char *kind_name(Value v) {
    switch (v.kind) {
    case VAL_NULL: return "null";
    case VAL_BOOL: return "bool";
    case VAL_INT: return "int";
    case VAL_FLOAT: return "float";
    case VAL_OBJ: return "string";
    }
    return "?";
}

static bool fail_operands(Interp *in, const Expr *at, TokenKind op, Value a, Value b) {
    char summary[160];
    snprintf(summary, sizeof summary, "cannot apply `%s` to %s and %s",
             op_symbol(op), kind_name(a), kind_name(b));
    const char *help = NULL;
    if ((a.kind == VAL_OBJ) != (b.kind == VAL_OBJ) && op == TOK_PLUS) {
        help = "both sides of `+` must be strings or both numbers";
    }
    return fail(in, at, summary, "here", help);
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
    if (op == TOK_PLUS && a.kind == VAL_OBJ && b.kind == VAL_OBJ) {
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
    case VAL_OBJ: {
        ObjString *x = (ObjString *)a.as.obj, *y = (ObjString *)b.as.obj;
        return x->length == y->length && memcmp(x->chars, y->chars, x->length) == 0;
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
    if (a.kind == VAL_OBJ && b.kind == VAL_OBJ) {
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

static bool need_bool(Interp *in, const Expr *at, Value v, const char *what) {
    if (v.kind == VAL_BOOL) return true;
    char summary[120], help[120];
    snprintf(summary, sizeof summary, "%s must be bool, found %s", what, kind_name(v));
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
    snprintf(summary, sizeof summary, "cannot negate %s", kind_name(v));
    value_release(v);
    return fail(in, e, summary, "here", NULL);
}

/* The only built-in so far. Prints one value and a newline. */
static bool call_print(Interp *in, const Expr *call, Value *out) {
    Value v;
    if (!eval(in, call->as.call.args[0], &v)) return false;
    value_print(v, stdout);
    fputc('\n', stdout);
    value_release(v);
    *out = value_null();
    return true;
}

/* Evaluates an expression into a value the caller owns (must release). */
static bool eval(Interp *in, const Expr *e, Value *out) {
    switch (e->kind) {
    case EXPR_INT:    *out = value_int(e->as.integer); return true;
    case EXPR_FLOAT:  *out = value_float(e->as.number); return true;
    case EXPR_BOOL:   *out = value_bool(e->as.boolean); return true;
    case EXPR_STRING: *out = value_string(e->as.string.chars, e->as.string.length); return true;
    case EXPR_NAME: {
        Binding *b = env_lookup(in->env, e->as.name.name);
        if (b == NULL) {
            char summary[160];
            snprintf(summary, sizeof summary, "`%s` has no value", e->as.name.name);
            return fail(in, e, summary, "its declaration failed earlier", NULL);
        }
        value_retain(b->value);
        *out = b->value;
        return true;
    }
    case EXPR_CALL:    return call_print(in, e, out);   /* check.c guarantees it is print */
    case EXPR_UNARY:   return eval_unary(in, e, out);
    case EXPR_BINARY:  return eval_binary(in, e, out);
    case EXPR_LOGICAL: return eval_logical(in, e, out);
    case EXPR_TERNARY: {
        Value c;
        if (!eval(in, e->as.ternary.cond, &c)) return false;
        if (!need_bool(in, e->as.ternary.cond, c, "the condition of `if`")) return false;
        return eval(in, c.as.boolean ? e->as.ternary.then : e->as.ternary.otherwise, out);
    }
    }
    *out = value_null();
    return true;
}

/* ---- statements ------------------------------------------------------- */

static bool exec_block(Interp *in, const StmtList *block);

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

static bool exec_stmt(Interp *in, const Stmt *s) {
    switch (s->kind) {
    case STMT_EXPR: {
        Value v;
        if (!eval(in, s->as.expr, &v)) return false;
        value_release(v);
        return true;
    }
    case STMT_LET: {
        Value v;
        if (!eval(in, s->as.let.value, &v)) return false;
        if (strcmp(s->as.let.name, "_") == 0) {
            value_release(v);                      /* `_` discards the value */
            return true;
        }
        env_define(in->env, s->as.let.name, v, s->as.let.is_mutable);
        return true;
    }
    case STMT_ASSIGN: {
        Value v;
        if (!eval(in, s->as.assign.value, &v)) return false;
        Binding *b = env_lookup(in->env, s->as.assign.name);   /* check.c: exists, mutable */
        if (b == NULL) {
            value_release(v);
            diag_set(in->err, s->line, s->column, "assignment to a name without a value",
                     "its declaration failed earlier", NULL);
            return false;
        }
        if (s->as.assign.op != TOK_EQUAL) {
            Value result;
            bool ok = arith(in, s->as.assign.value, base_op(s->as.assign.op), b->value, v, &result);
            value_release(v);
            if (!ok) return false;
            v = result;
        }
        value_release(b->value);
        b->value = v;
        return true;
    }
    case STMT_IF: {
        for (size_t i = 0; i < s->as.if_.count; i++) {
            Value c;
            if (!eval(in, s->as.if_.conds[i], &c)) return false;
            if (!need_bool(in, s->as.if_.conds[i], c, "the condition of `if`")) return false;
            if (c.as.boolean) return exec_block(in, s->as.if_.blocks[i]);
        }
        if (s->as.if_.otherwise != NULL) return exec_block(in, s->as.if_.otherwise);
        return true;
    }
    }
    return true;
}

/* A block is a scope: its variables are released when it ends. */
static bool exec_block(Interp *in, const StmtList *block) {
    Env *inner = env_new(in->env);
    in->env = inner;
    bool ok = true;
    for (size_t i = 0; ok && i < block->count; i++) ok = exec_stmt(in, block->items[i]);
    in->env = inner->parent;
    env_free(inner);
    return ok;
}

int interp_run(Interp *in, const Program *prog, Diag *err) {
    in->err = err;
    in->env = in->globals;
    for (size_t i = 0; i < prog->body.count; i++) {
        if (!exec_stmt(in, prog->body.items[i])) {
            fflush(stdout);
            return 1;
        }
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        fputs("error: cannot write output\n", stderr);
        return 1;
    }
    return 0;
}
