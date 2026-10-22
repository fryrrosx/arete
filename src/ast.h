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

typedef struct Expr Expr;
typedef struct Stmt Stmt;

typedef struct {
    Stmt **items;      /* owned array of owned statements */
    size_t count;
    size_t capacity;
    int slot_count;    /* resolver: names declared directly in this scope */
} StmtList;

/* One parameter of a function or lambda. */
typedef struct {
    char *name;            /* owned */
    TypeExpr *type;        /* NULL only in a lambda */
    Expr *default_value;   /* NULL when the argument is required */
    int line;
    int column;
} Param;

/* The static shape of a function, shared by `fn` declarations and lambdas.
 * A run-time function object points at one of these, so a program that
 * declared functions must outlive the values it created (main.c keeps REPL
 * programs until the session ends). */
typedef struct {
    char *name;            /* owned; NULL for a lambda */
    Param *params;
    size_t param_count;
    TypeExpr *result;      /* fn: NULL means it returns nothing; lambda: optional annotation */
    StmtList *body;        /* fn: the block; NULL for a lambda */
    Expr *expr_body;       /* lambda: the expression; NULL for a fn */
    int slot_count;        /* resolver: parameters plus locals declared directly in the body */
    const void *source;    /* the Source this function was parsed from (see parser.h) */
    int line;
    int column;
} FnProto;

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
    EXPR_TERNARY,    /* a if c else b */
    EXPR_RANGE,      /* a..b, a..=b */
    EXPR_LAMBDA      /* fn(x) => x * 2 */
} ExprKind;

/* How the resolver marks a name: `hops` environments outwards, then slot
 * number `slot`. SCOPE_GLOBAL means "look the name up in the global
 * scope"; SCOPE_UNRESOLVED is what the parser leaves. */
enum { SCOPE_UNRESOLVED = -2, SCOPE_GLOBAL = -1 };

struct Expr {
    ExprKind kind;
    int line;
    int column;
    union {
        int64_t integer;
        double number;
        bool boolean;
        struct { char *chars; size_t length; } string;    /* owned */
        struct { char *name; int hops; int slot; } name;   /* owned */
        struct {
            Expr *callee;
            Expr **args;
            char **names;       /* names[i] is NULL for a positional argument */
            size_t arg_count;
        } call;
        struct { TokenKind op; Expr *operand; } unary;
        struct { TokenKind op; Expr *left; Expr *right; } binary;
        struct { TokenKind op; Expr *left; Expr *right; } logical;
        struct { Expr *cond; Expr *then; Expr *otherwise; } ternary;
        struct { Expr *start; Expr *end; bool inclusive; } range;
        struct { FnProto *proto; } lambda;
    } as;
};

/* ---- statements ------------------------------------------------------- */

typedef enum {
    STMT_EXPR,         /* an expression on its own line */
    STMT_LET,          /* let x: T = e   /  var x = e */
    STMT_ASSIGN,       /* x = e, x += e, ... */
    STMT_IF,           /* if / elif / else */
    STMT_WHILE,        /* while cond: */
    STMT_FOR,          /* for x in iterable: */
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_RETURN,       /* return [e] */
    STMT_FN            /* fn name(params) -> T: */
} StmtKind;

struct Stmt {
    StmtKind kind;
    int line;
    int column;
    union {
        Expr *expr;
        struct { char *name; bool is_mutable; TypeExpr *type; Expr *value; int slot; } let;
        struct { char *name; TokenKind op; Expr *value; int hops; int slot; } assign;
        struct {
            Expr **conds;          /* the if and every elif condition */
            StmtList **blocks;     /* one block per condition */
            size_t count;
            StmtList *otherwise;   /* the else block, or NULL */
        } if_;
        struct { Expr *cond; StmtList *body; } while_;
        struct {
            char *name;            /* the loop variable, slot 0 of the body scope */
            int name_line;
            int name_column;
            Expr *iterable;
            StmtList *body;
        } for_;
        struct { Expr *value; } return_;     /* value is NULL for a bare return */
        struct { FnProto *proto; int slot; } fn;
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

FnProto *fn_proto_new(int line, int column);
void fn_proto_free(FnProto *f);
void fn_proto_add_param(FnProto *f, Param p);

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
