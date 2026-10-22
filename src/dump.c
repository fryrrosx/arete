#include "dump.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "arete_rt.h"

void dump_tokens(const TokenList *tokens, FILE *out) {
    for (size_t i = 0; i < tokens->count; i++) {
        const Token *t = &tokens->items[i];
        fprintf(out, "%d:%d %s", t->line, t->column, token_kind_name(t->kind));
        if (t->text != NULL) fprintf(out, " %s", t->text);
        fputc('\n', out);
    }
}

static void dump_string(const char *s, size_t n, FILE *out) {
    fputc('"', out);
    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
        case '"':  fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        case '\n': fputs("\\n", out); break;
        case '\t': fputs("\\t", out); break;
        default:   fputc(s[i], out); break;
        }
    }
    fputc('"', out);
}

/* Types print the way they are written: int, int?, int | string,
 * List<int>, fn(int) -> string throws. A function type or a union inside
 * another type gets parentheses, so `(int | string)?` and `int | string?`
 * never print the same. */
static void dump_type(const TypeExpr *t, FILE *out, bool nested) {
    switch (t->kind) {
    case TYPE_NAME:
        fputs(t->name, out);
        if (t->item_count > 0) {
            fputc('<', out);
            for (size_t i = 0; i < t->item_count; i++) {
                if (i > 0) fputs(", ", out);
                dump_type(t->items[i], out, false);
            }
            fputc('>', out);
        }
        break;
    case TYPE_NULLABLE:
        dump_type(t->inner, out, true);
        fputc('?', out);
        break;
    case TYPE_UNION:
        if (nested) fputc('(', out);
        for (size_t i = 0; i < t->item_count; i++) {
            if (i > 0) fputs(" | ", out);
            dump_type(t->items[i], out, true);
        }
        if (nested) fputc(')', out);
        break;
    case TYPE_FN:
        if (nested) fputc('(', out);
        fputs("fn(", out);
        for (size_t i = 0; i < t->item_count; i++) {
            if (i > 0) fputs(", ", out);
            dump_type(t->items[i], out, false);
        }
        fputc(')', out);
        if (t->inner != NULL) {
            fputs(" -> ", out);
            dump_type(t->inner, out, false);
        }
        if (t->throws) {
            fputs(" throws", out);
            if (t->throws_name != NULL) fprintf(out, " %s", t->throws_name);
        }
        if (nested) fputc(')', out);
        break;
    }
}

static void dump_expr(const Expr *e, FILE *out);

/* (a: int b: string = "x") -> T */
static void dump_signature(const FnProto *f, FILE *out) {
    fputc('(', out);
    for (size_t i = 0; i < f->param_count; i++) {
        const Param *p = &f->params[i];
        if (i > 0) fputc(' ', out);
        fputs(p->name, out);
        if (p->type != NULL) {
            fputs(": ", out);
            dump_type(p->type, out, false);
        }
        if (p->default_value != NULL) {
            fputs(" = ", out);
            dump_expr(p->default_value, out);
        }
    }
    fputc(')', out);
    if (f->result != NULL) {
        fputs(" -> ", out);
        dump_type(f->result, out, false);
    }
}

static void dump_expr(const Expr *e, FILE *out) {
    switch (e->kind) {
    case EXPR_INT:
        fprintf(out, "%" PRId64, e->as.integer);
        break;
    case EXPR_FLOAT:
        value_print(value_float(e->as.number), out);
        break;
    case EXPR_STRING:
        dump_string(e->as.string.chars, e->as.string.length, out);
        break;
    case EXPR_BOOL:
        fputs(e->as.boolean ? "true" : "false", out);
        break;
    case EXPR_NAME:
        fputs(e->as.name.name, out);
        break;
    case EXPR_CALL:
        fputc('(', out);
        dump_expr(e->as.call.callee, out);
        for (size_t i = 0; i < e->as.call.arg_count; i++) {
            fputc(' ', out);
            if (e->as.call.names[i] != NULL) fprintf(out, "%s: ", e->as.call.names[i]);
            dump_expr(e->as.call.args[i], out);
        }
        fputc(')', out);
        break;
    case EXPR_UNARY:
        fprintf(out, "(%s ", op_symbol(e->as.unary.op));
        dump_expr(e->as.unary.operand, out);
        fputc(')', out);
        break;
    case EXPR_BINARY:
    case EXPR_LOGICAL:
        fprintf(out, "(%s ", op_symbol(e->as.binary.op));
        dump_expr(e->as.binary.left, out);
        fputc(' ', out);
        dump_expr(e->as.binary.right, out);
        fputc(')', out);
        break;
    case EXPR_TERNARY:
        fputs("(ternary ", out);
        dump_expr(e->as.ternary.cond, out);
        fputc(' ', out);
        dump_expr(e->as.ternary.then, out);
        fputc(' ', out);
        dump_expr(e->as.ternary.otherwise, out);
        fputc(')', out);
        break;
    case EXPR_RANGE:
        fprintf(out, "(%s ", e->as.range.inclusive ? "..=" : "..");
        dump_expr(e->as.range.start, out);
        fputc(' ', out);
        dump_expr(e->as.range.end, out);
        fputc(')', out);
        break;
    case EXPR_WIDEN:
        fputs("(float ", out);
        dump_expr(e->as.widen.operand, out);
        fputc(')', out);
        break;
    case EXPR_LAMBDA:
        fputs("(lambda ", out);
        dump_signature(e->as.lambda.proto, out);
        fputc(' ', out);
        dump_expr(e->as.lambda.proto->expr_body, out);
        fputc(')', out);
        break;
    }
}

static void dump_block(const StmtList *block, FILE *out);

static void dump_stmt(const Stmt *s, FILE *out) {
    switch (s->kind) {
    case STMT_EXPR:
        dump_expr(s->as.expr, out);
        break;
    case STMT_LET:
        fprintf(out, "(%s %s", s->as.let.is_mutable ? "var" : "let", s->as.let.name);
        if (s->as.let.type != NULL) {
            fputs(": ", out);
            dump_type(s->as.let.type, out, false);
        }
        fputc(' ', out);
        dump_expr(s->as.let.value, out);
        fputc(')', out);
        break;
    case STMT_ASSIGN:
        fprintf(out, "(%s %s ", op_symbol(s->as.assign.op), s->as.assign.name);
        dump_expr(s->as.assign.value, out);
        fputc(')', out);
        break;
    case STMT_IF:
        fputs("(if", out);
        for (size_t i = 0; i < s->as.if_.count; i++) {
            if (i > 0) fputs(" elif", out);
            fputc(' ', out);
            dump_expr(s->as.if_.conds[i], out);
            fputc(' ', out);
            dump_block(s->as.if_.blocks[i], out);
        }
        if (s->as.if_.otherwise != NULL) {
            fputs(" else ", out);
            dump_block(s->as.if_.otherwise, out);
        }
        fputc(')', out);
        break;
    case STMT_WHILE:
        fputs("(while ", out);
        dump_expr(s->as.while_.cond, out);
        fputc(' ', out);
        dump_block(s->as.while_.body, out);
        fputc(')', out);
        break;
    case STMT_FOR:
        fprintf(out, "(for %s ", s->as.for_.name);
        dump_expr(s->as.for_.iterable, out);
        fputc(' ', out);
        dump_block(s->as.for_.body, out);
        fputc(')', out);
        break;
    case STMT_BREAK:
        fputs("(break)", out);
        break;
    case STMT_CONTINUE:
        fputs("(continue)", out);
        break;
    case STMT_RETURN:
        fputs("(return", out);
        if (s->as.return_.value != NULL) {
            fputc(' ', out);
            dump_expr(s->as.return_.value, out);
        }
        fputc(')', out);
        break;
    case STMT_FN:
        fprintf(out, "(fn %s ", s->as.fn.proto->name);
        dump_signature(s->as.fn.proto, out);
        fputc(' ', out);
        dump_block(s->as.fn.proto->body, out);
        fputc(')', out);
        break;
    }
}

static void dump_block(const StmtList *block, FILE *out) {
    fputs("(block", out);
    for (size_t i = 0; i < block->count; i++) {
        fputc(' ', out);
        dump_stmt(block->items[i], out);
    }
    fputc(')', out);
}

void dump_program(const Program *prog, FILE *out) {
    for (size_t i = 0; i < prog->body.count; i++) {
        dump_stmt(prog->body.items[i], out);
        fputc('\n', out);
    }
}
