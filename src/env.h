/* env.h - scopes at run time. The resolver (check.c) numbers every local
 * name as (hops, slot): walk `hops` parents outwards, take slot `slot`.
 * The global scope keeps names, because the REPL adds to it line by line
 * and a program's top-level functions must see names declared later. */
#ifndef ARETE_ENV_H
#define ARETE_ENV_H

#include <stdbool.h>
#include <stddef.h>

#include "arete_rt.h"

/* A new scope with `slot_count` empty slots; holds one reference to
 * `parent`. The caller owns the returned reference. */
ObjEnv *env_new(ObjEnv *parent, int slot_count);

void env_retain(ObjEnv *env);
void env_release(ObjEnv *env);

/* Frees the scope's contents and the scope. Called by value_release at
 * refcount zero and by rt_shutdown; not for direct use. */
void env_destroy(ObjEnv *env);

/* The slot `hops` scopes outwards. */
Value *env_slot(ObjEnv *env, int hops, int slot);

/* Global scope by name. Defining a name already present rebinds it (the
 * REPL needs this). Lookup returns NULL when the name is unbound. */
void env_define_global(ObjEnv *global, const char *name, Value value, bool is_mutable);
Binding *env_find_global(ObjEnv *global, const char *name);

#endif
