/* arete_rt.h - the public runtime API: how values are represented and how
 * memory is counted. First version, phase 3; functions, ranges and
 * environments added in phase 7. The standard library (phase 21) and the
 * bytecode VM (phase 27) use only what is declared here.
 *
 * Memory rule (SPEC.md): automatic reference counting from day one. Every
 * heap object carries `refcount`; `value_retain` adds one owner,
 * `value_release` removes one and frees the object at zero. Closures make
 * cycles (an environment holds a function that holds the environment);
 * a cycle collector is phase 29, so until then `rt_shutdown` frees every
 * environment and function that is still alive when the program ends. */
#ifndef ARETE_RT_H
#define ARETE_RT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    OBJ_STRING,
    OBJ_RANGE,
    OBJ_FUNCTION,
    OBJ_ENV
} ObjKind;

/* Header shared by every heap object. */
typedef struct Obj {
    ObjKind kind;
    uint32_t refcount;
} Obj;

/* An immutable UTF-8 string. `chars` is NUL-terminated for convenience,
 * but `length` is the truth: strings may contain any bytes. */
typedef struct {
    Obj obj;
    size_t length;
    char chars[];
} ObjString;

typedef enum {
    VAL_UNSET,     /* a slot nothing was stored in yet; never a value a program sees */
    VAL_NULL,
    VAL_BOOL,
    VAL_INT,
    VAL_FLOAT,
    VAL_OBJ
} ValueKind;

/* A value: a tag plus 8 bytes of payload. Ints and floats live inline,
 * everything else is a pointer to a counted heap object. */
typedef struct {
    ValueKind kind;
    union {
        bool boolean;
        int64_t integer;
        double number;
        Obj *obj;
    } as;
} Value;

/* `start..end` or `start..=end`; the ends are ints. */
typedef struct {
    Obj obj;
    int64_t start;
    int64_t end;
    bool inclusive;
} ObjRange;

/* A name bound in the global scope (the only scope that keeps names at
 * run time; every other scope is addressed by slot). */
typedef struct {
    char *name;      /* owned */
    Value value;     /* one reference owned by the scope */
    bool is_mutable;
} Binding;

/* A scope at run time. Local scopes hold `slot_count` values that the
 * resolver numbered; the global scope holds named bindings instead, so
 * that the REPL can add names at any time. */
typedef struct ObjEnv {
    Obj obj;
    struct ObjEnv *parent;    /* one reference held */
    Value *slots;
    int slot_count;
    Binding *names;
    size_t name_count;
    size_t name_capacity;
    size_t *name_index;       /* hash table over names: binding index + 1, 0 is empty */
    size_t name_index_size;
    struct ObjEnv *track_prev, *track_next;   /* the list rt_shutdown sweeps */
} ObjEnv;

/* A function value: a user function (its static shape plus the scope it
 * closed over) or a built-in such as print. */
typedef struct ObjFunction {
    Obj obj;
    const char *name;         /* borrowed; "<lambda>" for a lambda */
    const void *proto;        /* the FnProto from the syntax tree; NULL for a built-in */
    ObjEnv *closure;          /* one reference held; NULL for a built-in */
    int native;               /* 0 for a user function, otherwise a built-in id */
    struct ObjFunction *track_prev, *track_next;
} ObjFunction;

Value value_null(void);
Value value_bool(bool b);
Value value_int(int64_t i);
Value value_float(double f);

/* Copies `length` bytes into a new string with refcount 1. */
Value value_string(const char *chars, size_t length);

/* A new string holding a followed by b (both must be strings). */
Value value_string_concat(Value a, Value b);

Value value_range(int64_t start, int64_t end, bool inclusive);

/* A new function value; takes one reference to `closure` (may be NULL). */
Value value_function(const char *name, const void *proto, ObjEnv *closure, int native);

/* True when the value is a heap object of this kind. */
bool value_is(Value v, ObjKind kind);

void value_retain(Value v);
void value_release(Value v);

/* Writes the value the way `print` shows it (SPEC.md section 2.4). */
void value_print(Value v, FILE *out);

/* The type name used in messages: int, float, string, bool, range, fn. */
const char *value_type_name(Value v);

/* Every environment registers itself for the final sweep (env.c calls these). */
void rt_track_env(ObjEnv *env);
void rt_untrack_env(ObjEnv *env);

/* True while rt_shutdown sweeps: an environment or function being freed
 * then must not release the (possibly already freed) ones it points to. */
bool rt_shutting_down(void);

/* Frees every environment and function still alive, cycles included.
 * Called once, when the interpreter shuts down; nothing may run after it. */
void rt_shutdown(void);

#endif
