/* ast.h - the syntax tree the parser builds and the interpreter walks. */
#ifndef ARETE_AST_H
#define ARETE_AST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "token.h"

/* ---- types (parsed from phase 5, checked from phase 10) ---------------- */

typedef enum {
    TYPE_NAME,      /* int, string, List<int> */
    TYPE_NULLABLE,  /* T? */
    TYPE_UNION,     /* A | B */
    TYPE_FN         /* fn(int) -> string throws */
} TypeKind;

typedef struct TypeExpr TypeExpr;

struct TypeExpr {
    TypeKind kind;
    int line;
    int column;
    char *name;          /* TYPE_NAME: owned */
    TypeExpr **items;    /* NAME: type arguments; UNION: members; FN: parameters */
    size_t item_count;
    TypeExpr *inner;     /* NULLABLE: the type; FN: the result, or NULL */
    bool throws;         /* FN */
    char *throws_name;   /* FN: owned, may be NULL */
};

/* ---- expressions ------------------------------------------------------ */

typedef enum {
    EXPR_INT,
    EXPR_FLOAT,
    EXPR_STRING,
    EXPR_BOOL,
    EXPR_NAME,
    EXPR_CALL,
    EXPR_UNARY,      /* -x, not x */
    EXPR_BINARY,     /* a + b, a < b */
    EXPR_LOGICAL,    /* a and b, a or b (short-circuit) */
    EXPR_TERNARY     /* a if c else b */
} ExprKind;

typedef struct Expr Expr;

struct Expr {
    ExprKind kind;
    int line;
    int column;
    union {
        int64_t integer;
        double number;
        bool boolean;
        struct { char *chars; size_t length; } string;    /* owned */
        struct { char *name; } name;                        /* owned */
        struct { char *name; Expr **args; size_t arg_count; } call;
        struct { TokenKind op; Expr *operand; } unary;
        struct { TokenKind op; Expr *left; Expr *right; } binary;
        struct { TokenKind op; Expr *left; Expr *right; } logical;
        struct { Expr *cond; Expr *then; Expr *otherwise; } ternary;
    } as;
};

/* ---- statements ------------------------------------------------------- */

typedef struct Stmt Stmt;

typedef struct {
    Stmt **items;      /* owned array of owned statements */
    size_t count;
    size_t capacity;
} StmtList;

typedef enum {
    STMT_EXPR,         /* an expression on its own line */
    STMT_LET,          /* let x: T = e   /  var x = e */
    STMT_ASSIGN,       /* x = e, x += e, ... */
    STMT_IF            /* if / elif / else */
} StmtKind;

struct Stmt {
    StmtKind kind;
    int line;
    int column;
    union {
        Expr *expr;
        struct { char *name; bool is_mutable; TypeExpr *type; Expr *value; } let;
        struct { char *name; TokenKind op; Expr *value; } assign;
        struct {
            Expr **conds;          /* the if and every elif condition */
            StmtList **blocks;     /* one block per condition */
            size_t count;
            StmtList *otherwise;   /* the else block, or NULL */
        } if_;
    } as;
};

typedef struct {
    char edition[16];  /* the version from line 1, "0.1"; empty for a REPL line */
    StmtList body;
} Program;

/* Constructors take ownership of malloc'd strings and arrays passed in. */
TypeExpr *type_new(TypeKind kind, int line, int column);
void type_free(TypeExpr *t);

Expr *expr_new(ExprKind kind, int line, int column);
void expr_free(Expr *e);

Stmt *stmt_new(StmtKind kind, int line, int column);
void stmt_free(Stmt *s);

void stmt_list_init(StmtList *list);
void stmt_list_add(StmtList *list, Stmt *s);
void stmt_list_free_items(StmtList *list);
StmtList *stmt_list_new(void);
void stmt_list_free(StmtList *list);

Program *program_new(void);
void program_free(Program *p);

/* Operator names for messages and dumps: "+", "not", "==". */
const char *op_symbol(TokenKind op);

#endif
