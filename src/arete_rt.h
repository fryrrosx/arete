/* arete_rt.h - the public runtime API: how values are represented and how
 * memory is counted. First version, phase 3. The standard library (phase 21)
 * and the bytecode VM (phase 27) use only what is declared here.
 *
 * Memory rule (SPEC.md): automatic reference counting from day one. Every
 * heap object carries `refcount`; `value_retain` adds one owner,
 * `value_release` removes one and frees the object at zero. */
#ifndef ARETE_RT_H
#define ARETE_RT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    OBJ_STRING
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

Value value_null(void);
Value value_bool(bool b);
Value value_int(int64_t i);
Value value_float(double f);

/* Copies `length` bytes into a new string with refcount 1. */
Value value_string(const char *chars, size_t length);

/* A new string holding a followed by b (both must be strings). */
Value value_string_concat(Value a, Value b);

void value_retain(Value v);
void value_release(Value v);

/* Writes the value the way `print` shows it (SPEC.md section 2.4). */
void value_print(Value v, FILE *out);

#endif
