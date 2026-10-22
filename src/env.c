#include "env.h"

#include <stdlib.h>
#include <string.h>

#include "util.h"

ObjEnv *env_new(ObjEnv *parent, int slot_count) {
    ObjEnv *env = xcalloc(sizeof *env);
    env->obj.kind = OBJ_ENV;
    env->obj.refcount = 1;
    env->parent = parent;
    if (parent != NULL) env_retain(parent);
    env->slot_count = slot_count;
    if (slot_count > 0) {
        env->slots = xcalloc((size_t)slot_count * sizeof(Value));   /* all VAL_NULL */
    }
    rt_track_env(env);
    return env;
}

static Value env_value(ObjEnv *env) {
    Value v;
    v.kind = VAL_OBJ;
    v.as.obj = &env->obj;
    return v;
}

void env_retain(ObjEnv *env) {
    value_retain(env_value(env));
}

void env_release(ObjEnv *env) {
    value_release(env_value(env));
}

void env_destroy(ObjEnv *env) {
    rt_untrack_env(env);
    for (int i = 0; i < env->slot_count; i++) value_release(env->slots[i]);
    free(env->slots);
    for (size_t i = 0; i < env->name_count; i++) {
        free(env->names[i].name);
        value_release(env->names[i].value);
    }
    free(env->names);
    free(env->name_index);
    if (env->parent != NULL && !rt_shutting_down()) env_release(env->parent);
    free(env);
}

Value *env_slot(ObjEnv *env, int hops, int slot) {
    for (int i = 0; i < hops; i++) env = env->parent;
    return &env->slots[slot];
}

static size_t hash_name(const char *name) {
    size_t h = 1469598103934665603u;                   /* FNV-1a */
    for (; *name != '\0'; name++) h = (h ^ (unsigned char)*name) * 1099511628211u;
    return h;
}

static void index_insert(ObjEnv *g, size_t item) {
    size_t at = hash_name(g->names[item].name) & (g->name_index_size - 1);
    while (g->name_index[at] != 0) at = (at + 1) & (g->name_index_size - 1);
    g->name_index[at] = item + 1;
}

static void index_rebuild(ObjEnv *g) {
    size_t want = 16;
    while (want < g->name_count * 2) want *= 2;
    free(g->name_index);
    g->name_index = xcalloc(want * sizeof(size_t));
    g->name_index_size = want;
    for (size_t i = 0; i < g->name_count; i++) index_insert(g, i);
}

Binding *env_find_global(ObjEnv *global, const char *name) {
    if (global->name_index_size == 0) return NULL;
    size_t at = hash_name(name) & (global->name_index_size - 1);
    while (global->name_index[at] != 0) {
        Binding *b = &global->names[global->name_index[at] - 1];
        if (strcmp(b->name, name) == 0) return b;
        at = (at + 1) & (global->name_index_size - 1);
    }
    return NULL;
}

void env_define_global(ObjEnv *global, const char *name, Value value, bool is_mutable) {
    Binding *existing = env_find_global(global, name);
    if (existing != NULL) {
        value_release(existing->value);
        existing->value = value;
        existing->is_mutable = is_mutable;
        return;
    }
    if (global->name_count == global->name_capacity) {
        global->name_capacity = global->name_capacity == 0 ? 8 : global->name_capacity * 2;
        global->names = xrealloc(global->names, global->name_capacity * sizeof(Binding));
    }
    Binding *b = &global->names[global->name_count++];
    b->name = xstrdup(name);
    b->value = value;
    b->is_mutable = is_mutable;
    if (global->name_count * 2 > global->name_index_size) index_rebuild(global);
    else index_insert(global, global->name_count - 1);
}
