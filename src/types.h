/* types.h - the static types of phase 10: what the checker knows about a
 * value before the program runs. Types are owned by a TypeArena that lives
 * as long as the checker (a REPL session), so a Type pointer stored in a
 * declaration stays valid. */
#ifndef ARETE_TYPES_H
#define ARETE_TYPES_H

#include <stdbool.h>
#include <stddef.h>

#include "ast.h"
#include "diag.h"

typedef enum {
    TY_INT,
    TY_FLOAT,
    TY_STRING,
    TY_BOOL,
    TY_RANGE,
    TY_FN,
    TY_NONE,      /* a call that returns nothing; never the type of a variable */
    TY_ANY        /* accepts any value: only the parameter of built-ins like print */
} TypeTag;

typedef struct Type {
    TypeTag tag;
    struct Type **params;     /* TY_FN */
    size_t param_count;
    struct Type *result;      /* TY_FN: NULL means it returns nothing */
    struct Type *next;        /* the arena's list */
} Type;

typedef struct {
    Type *all;                /* every function type made so far */
    Type simple[TY_ANY + 1];  /* one shared instance per simple tag */
} TypeArena;

void type_arena_init(TypeArena *a);
void type_arena_free(TypeArena *a);

Type *type_simple(TypeArena *a, TypeTag tag);

/* A function type; takes ownership of the `params` array. */
Type *type_fn(TypeArena *a, Type **params, size_t count, Type *result);

bool type_equal(const Type *a, const Type *b);

/* True when a value of type `from` may be used where `to` is expected:
 * equal types, anything for TY_ANY, and int where float is expected. */
bool type_accepts(const Type *to, const Type *from);

/* Writes the type the way a program spells it: int, fn(int) -> string;
 * "nothing" for TY_NONE. */
void type_name(const Type *t, char *buf, size_t cap);

/* Turns a written annotation into a type. Returns NULL and fills `err`
 * for an unknown name or a type of a later phase (`int?`, `List<int>`). */
Type *type_from_expr(TypeArena *a, const TypeExpr *te, Diag *err);

#endif
