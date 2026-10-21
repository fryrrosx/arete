#include "env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

Env *env_new(Env *parent) {
    Env *env = xcalloc(sizeof *env);
    env->parent = parent;
    return env;
}

void env_free(Env *env) {
    if (env == NULL) return;
    for (size_t i = 0; i < env->count; i++) {
        free(env->items[i].name);
        value_release(env->items[i].value);
    }
    free(env->items);
    free(env);
}

static Binding *find_here(Env *env, const char *name) {
    for (size_t i = 0; i < env->count; i++) {
        if (strcmp(env->items[i].name, name) == 0) return &env->items[i];
    }
    return NULL;
}

void env_define(Env *env, const char *name, Value value, bool is_mutable) {
    Binding *existing = find_here(env, name);
    if (existing != NULL) {
        value_release(existing->value);
        existing->value = value;
        existing->is_mutable = is_mutable;
        return;
    }
    if (env->count == env->capacity) {
        env->capacity = env->capacity == 0 ? 8 : env->capacity * 2;
        env->items = xrealloc(env->items, env->capacity * sizeof(Binding));
    }
    Binding *b = &env->items[env->count++];
    b->name = xstrdup(name);
    b->value = value;
    b->is_mutable = is_mutable;
}

Binding *env_lookup(Env *env, const char *name) {
    for (Env *e = env; e != NULL; e = e->parent) {
        Binding *b = find_here(e, name);
        if (b != NULL) return b;
    }
    return NULL;
}
