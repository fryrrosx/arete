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
    if (env->parent != NULL && !rt_shutting_down()) env_release(env->parent);
    free(env);
}

Value *env_slot(ObjEnv *env, int hops, int slot) {
    for (int i = 0; i < hops; i++) env = env->parent;
    return &env->slots[slot];
}

Binding *env_find_global(ObjEnv *global, const char *name) {
    for (size_t i = 0; i < global->name_count; i++) {
        if (strcmp(global->names[i].name, name) == 0) return &global->names[i];
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
}
