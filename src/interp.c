#include "interp.h"

#include <stdio.h>
#include <string.h>

#include "arete_rt.h"

static Value eval(const Expr *e);

/* The only built-in so far. Prints one value and a newline. */
static Value call_print(const Expr *call) {
    Value v = eval(call->as.call.args[0]);
    value_print(v, stdout);
    fputc('\n', stdout);
    value_release(v);
    return value_null();
}

/* Evaluates an expression into a value the caller owns (must release). */
static Value eval(const Expr *e) {
    switch (e->kind) {
    case EXPR_STRING:
        return value_string(e->as.string.chars, e->as.string.length);
    case EXPR_CALL:
        return call_print(e);              /* check.c guarantees it is print */
    case EXPR_NAME:
        break;                             /* rejected by check.c */
    }
    return value_null();
}

int interp_run(const Program *prog) {
    for (size_t i = 0; i < prog->count; i++) {
        Value v = eval(prog->stmts[i]->expr);
        value_release(v);
    }
    fflush(stdout);
    return 0;
}
