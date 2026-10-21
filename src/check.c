#include "check.h"

#include <stdio.h>
#include <string.h>

static bool check_expr(const Expr *e, bool used_as_value, Diag *err) {
    switch (e->kind) {
    case EXPR_STRING:
        return true;

    case EXPR_NAME: {
        char summary[160];
        snprintf(summary, sizeof summary, "unknown name `%s`", e->as.name.name);
        diag_set(err, e->line, e->column, summary, "nothing has this name",
                 "variables arrive in phase 5; to call a function write `name(...)`");
        return false;
    }

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
        for (size_t i = 0; i < e->as.call.arg_count; i++) {
            if (!check_expr(e->as.call.args[i], true, err)) return false;
        }
        return true;
    }
    }
    return true;
}

bool check_program(const Program *prog, Diag *err) {
    for (size_t i = 0; i < prog->count; i++) {
        const Stmt *s = prog->stmts[i];
        if (!check_expr(s->expr, false, err)) return false;
    }
    return true;
}
