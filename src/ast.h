/* ast.h - the syntax tree the parser builds and the interpreter walks. */
#ifndef ARETE_AST_H
#define ARETE_AST_H

#include <stddef.h>

typedef enum {
    EXPR_STRING,   /* "text" */
    EXPR_NAME,     /* print, x */
    EXPR_CALL      /* callee(args) */
} ExprKind;

typedef struct Expr Expr;

struct Expr {
    ExprKind kind;
    int line;
    int column;
    union {
        struct {
            char *chars;      /* owned; escapes already processed */
            size_t length;
        } string;
        struct {
            char *name;       /* owned */
        } name;
        struct {
            char *name;       /* owned; the callee's name */
            Expr **args;      /* owned array of owned expressions */
            size_t arg_count;
        } call;
    } as;
};

typedef enum {
    STMT_EXPR      /* an expression on its own line */
} StmtKind;

typedef struct {
    StmtKind kind;
    int line;
    int column;
    Expr *expr;    /* owned */
} Stmt;

typedef struct {
    Stmt **stmts;  /* owned array of owned statements */
    size_t count;
    size_t capacity;
} Program;

Expr *expr_new_string(int line, int column, const char *chars, size_t length);
Expr *expr_new_name(int line, int column, const char *name);
/* Takes ownership of `name` (malloc'd) and of the `args` array. */
Expr *expr_new_call(int line, int column, char *name, Expr **args, size_t arg_count);
void expr_free(Expr *e);

Stmt *stmt_new_expr(Expr *e);
void stmt_free(Stmt *s);

Program *program_new(void);
void program_add(Program *p, Stmt *s);
void program_free(Program *p);

#endif
