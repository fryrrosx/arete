/* env.h - variables at run time: a scope with a parent, each name bound to
 * a value and a mutability flag. Lookups walk outwards through parents. */
#ifndef ARETE_ENV_H
#define ARETE_ENV_H

#include <stdbool.h>
#include <stddef.h>

#include "arete_rt.h"

typedef struct {
    char *name;      /* owned */
    Value value;     /* one reference owned by the scope */
    bool is_mutable;
} Binding;

typedef struct Env {
    struct Env *parent;
    Binding *items;
    size_t count;
    size_t capacity;
} Env;

Env *env_new(Env *parent);
void env_free(Env *env);

/* Binds `name` in this scope, taking over one reference to `value`.
 * A name already bound in this same scope is rebound (the REPL needs this). */
void env_define(Env *env, const char *name, Value value, bool is_mutable);

/* Finds the binding in this scope or an enclosing one. NULL if unbound. */
Binding *env_lookup(Env *env, const char *name);

#endif
