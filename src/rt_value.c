#include "arete_rt.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

Value value_null(void) {
    Value v;
    v.kind = VAL_NULL;
    v.as.integer = 0;
    return v;
}

Value value_bool(bool b) {
    Value v;
    v.kind = VAL_BOOL;
    v.as.boolean = b;
    return v;
}

Value value_int(int64_t i) {
    Value v;
    v.kind = VAL_INT;
    v.as.integer = i;
    return v;
}

Value value_float(double f) {
    Value v;
    v.kind = VAL_FLOAT;
    v.as.number = f;
    return v;
}

Value value_string(const char *chars, size_t length) {
    ObjString *s = malloc(sizeof(ObjString) + length + 1);
    if (s == NULL) {
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    s->obj.kind = OBJ_STRING;
    s->obj.refcount = 1;
    s->length = length;
    memcpy(s->chars, chars, length);
    s->chars[length] = '\0';
    Value v;
    v.kind = VAL_OBJ;
    v.as.obj = &s->obj;
    return v;
}

void value_retain(Value v) {
    if (v.kind == VAL_OBJ) v.as.obj->refcount++;
}

void value_release(Value v) {
    if (v.kind != VAL_OBJ) return;
    Obj *obj = v.as.obj;
    if (--obj->refcount == 0) free(obj);
}

/* Shortest decimal that reads back to the same double, always with a
 * decimal point: 3.0, 3.5, 3.14159. */
static void print_float(double f, FILE *out) {
    if (isnan(f)) { fputs("nan", out); return; }
    if (isinf(f)) { fputs(f < 0 ? "-inf" : "inf", out); return; }
    char buf[32];
    for (int precision = 1; precision <= 17; precision++) {
        snprintf(buf, sizeof buf, "%.*g", precision, f);
        if (strtod(buf, NULL) == f) break;
    }
    fputs(buf, out);
    if (strpbrk(buf, ".eE") == NULL) fputs(".0", out);
}

void value_print(Value v, FILE *out) {
    switch (v.kind) {
    case VAL_NULL:
        fputs("null", out);
        break;
    case VAL_BOOL:
        fputs(v.as.boolean ? "true" : "false", out);
        break;
    case VAL_INT:
        fprintf(out, "%" PRId64, v.as.integer);
        break;
    case VAL_FLOAT:
        print_float(v.as.number, out);
        break;
    case VAL_OBJ: {
        ObjString *s = (ObjString *)v.as.obj;
        fwrite(s->chars, 1, s->length, out);
        break;
    }
    }
}
