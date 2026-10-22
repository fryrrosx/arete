#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/* ---- types ------------------------------------------------------------ */

TypeExpr *type_new(TypeKind kind, int line, int column) {
    TypeExpr *t = xcalloc(sizeof *t);
    t->kind = kind;
    t->line = line;
    t->column = column;
    return t;
}

void type_free(TypeExpr *t) {
    if (t == NULL) return;
    free(t->name);
    for (size_t i = 0; i < t->item_count; i++) type_free(t->items[i]);
    free(t->items);
    type_free(t->inner);
    free(t->throws_name);
    free(t);
}

/* ---- expressions ------------------------------------------------------ */

Expr *expr_new(ExprKind kind, int line, int column) {
    Expr *e = xcalloc(sizeof *e);
    e->kind = kind;
    e->line = line;
    e->column = column;
    return e;
}

void expr_free(Expr *e) {
    if (e == NULL) return;
    switch (e->kind) {
    case EXPR_INT:
    case EXPR_FLOAT:
    case EXPR_BOOL:
        break;
    case EXPR_STRING:
        free(e->as.string.chars);
        break;
    case EXPR_NAME:
        free(e->as.name.name);
        break;
    case EXPR_CALL:
        expr_free(e->as.call.callee);
        for (size_t i = 0; i < e->as.call.arg_count; i++) {
            expr_free(e->as.call.args[i]);
            free(e->as.call.names[i]);
        }
        free(e->as.call.args);
        free(e->as.call.names);
        break;
    case EXPR_UNARY:
        expr_free(e->as.unary.operand);
        break;
    case EXPR_BINARY:
        expr_free(e->as.binary.left);
        expr_free(e->as.binary.right);
        break;
    case EXPR_LOGICAL:
        expr_free(e->as.logical.left);
        expr_free(e->as.logical.right);
        break;
    case EXPR_TERNARY:
        expr_free(e->as.ternary.cond);
        expr_free(e->as.ternary.then);
        expr_free(e->as.ternary.otherwise);
        break;
    case EXPR_RANGE:
        expr_free(e->as.range.start);
        expr_free(e->as.range.end);
        break;
    case EXPR_LAMBDA:
        fn_proto_free(e->as.lambda.proto);
        break;
    case EXPR_WIDEN:
        expr_free(e->as.widen.operand);
        break;
    }
    free(e);
}

/* ---- functions -------------------------------------------------------- */

FnProto *fn_proto_new(int line, int column) {
    FnProto *f = xcalloc(sizeof *f);
    f->line = line;
    f->column = column;
    return f;
}

void fn_proto_add_param(FnProto *f, Param p) {
    f->params = xrealloc(f->params, (f->param_count + 1) * sizeof(Param));
    f->params[f->param_count++] = p;
}

void fn_proto_free(FnProto *f) {
    if (f == NULL) return;
    free(f->name);
    for (size_t i = 0; i < f->param_count; i++) {
        free(f->params[i].name);
        type_free(f->params[i].type);
        expr_free(f->params[i].default_value);
    }
    free(f->params);
    type_free(f->result);
    stmt_list_free(f->body);
    expr_free(f->expr_body);
    free(f);
}

/* ---- statements ------------------------------------------------------- */

Stmt *stmt_new(StmtKind kind, int line, int column) {
    Stmt *s = xcalloc(sizeof *s);
    s->kind = kind;
    s->line = line;
    s->column = column;
    return s;
}

void stmt_free(Stmt *s) {
    if (s == NULL) return;
    switch (s->kind) {
    case STMT_EXPR:
        expr_free(s->as.expr);
        break;
    case STMT_LET:
        free(s->as.let.name);
        type_free(s->as.let.type);
        expr_free(s->as.let.value);
        break;
    case STMT_ASSIGN:
        free(s->as.assign.name);
        expr_free(s->as.assign.value);
        break;
    case STMT_IF:
        for (size_t i = 0; i < s->as.if_.count; i++) {
            expr_free(s->as.if_.conds[i]);
            stmt_list_free(s->as.if_.blocks[i]);
        }
        free(s->as.if_.conds);
        free(s->as.if_.blocks);
        stmt_list_free(s->as.if_.otherwise);
        break;
    case STMT_WHILE:
        expr_free(s->as.while_.cond);
        stmt_list_free(s->as.while_.body);
        break;
    case STMT_FOR:
        free(s->as.for_.name);
        expr_free(s->as.for_.iterable);
        stmt_list_free(s->as.for_.body);
        break;
    case STMT_BREAK:
    case STMT_CONTINUE:
        break;
    case STMT_RETURN:
        expr_free(s->as.return_.value);
        break;
    case STMT_FN:
        fn_proto_free(s->as.fn.proto);
        break;
    }
    free(s);
}

void stmt_list_init(StmtList *list) {
    list->items = NULL;
    list->count = list->capacity = 0;
    list->slot_count = 0;
}

void stmt_list_add(StmtList *list, Stmt *s) {
    if (list->count == list->capacity) {
        list->capacity = list->capacity == 0 ? 8 : list->capacity * 2;
        list->items = xrealloc(list->items, list->capacity * sizeof(Stmt *));
    }
    list->items[list->count++] = s;
}

void stmt_list_free_items(StmtList *list) {
    for (size_t i = 0; i < list->count; i++) stmt_free(list->items[i]);
    free(list->items);
    stmt_list_init(list);
}

StmtList *stmt_list_new(void) {
    StmtList *list = xcalloc(sizeof *list);
    stmt_list_init(list);
    return list;
}

void stmt_list_free(StmtList *list) {
    if (list == NULL) return;
    stmt_list_free_items(list);
    free(list);
}

Program *program_new(void) {
    Program *p = xcalloc(sizeof *p);
    stmt_list_init(&p->body);
    return p;
}

void program_free(Program *p) {
    if (p == NULL) return;
    stmt_list_free_items(&p->body);
    free(p);
}

const char *op_symbol(TokenKind op) {
    switch (op) {
    case TOK_PLUS: return "+";
    case TOK_MINUS: return "-";
    case TOK_STAR: return "*";
    case TOK_SLASH: return "/";
    case TOK_SLASH_SLASH: return "//";
    case TOK_STAR_STAR: return "**";
    case TOK_EQUAL: return "=";
    case TOK_PLUS_EQUAL: return "+=";
    case TOK_MINUS_EQUAL: return "-=";
    case TOK_STAR_EQUAL: return "*=";
    case TOK_SLASH_EQUAL: return "/=";
    case TOK_SLASH_SLASH_EQUAL: return "//=";
    case TOK_EQUAL_EQUAL: return "==";
    case TOK_BANG_EQUAL: return "!=";
    case TOK_LESS: return "<";
    case TOK_LESS_EQUAL: return "<=";
    case TOK_GREATER: return ">";
    case TOK_GREATER_EQUAL: return ">=";
    case TOK_AND: return "and";
    case TOK_OR: return "or";
    case TOK_NOT: return "not";
    case TOK_DOT_DOT: return "..";
    case TOK_DOT_DOT_EQUAL: return "..=";
    default: return "?";
    }
}
