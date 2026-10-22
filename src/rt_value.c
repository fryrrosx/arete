#include "arete_rt.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "env.h"
#include "util.h"

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
    ObjString *s = xmalloc(sizeof(ObjString) + length + 1);
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

Value value_string_concat(Value a, Value b) {
    ObjString *x = (ObjString *)a.as.obj;
    ObjString *y = (ObjString *)b.as.obj;
    size_t length = x->length + y->length;
    ObjString *s = xmalloc(sizeof(ObjString) + length + 1);
    s->obj.kind = OBJ_STRING;
    s->obj.refcount = 1;
    s->length = length;
    memcpy(s->chars, x->chars, x->length);
    memcpy(s->chars + x->length, y->chars, y->length);
    s->chars[length] = '\0';
    Value v;
    v.kind = VAL_OBJ;
    v.as.obj = &s->obj;
    return v;
}

Value value_range(int64_t start, int64_t end, bool inclusive) {
    ObjRange *r = xmalloc(sizeof *r);
    r->obj.kind = OBJ_RANGE;
    r->obj.refcount = 1;
    r->start = start;
    r->end = end;
    r->inclusive = inclusive;
    Value v;
    v.kind = VAL_OBJ;
    v.as.obj = &r->obj;
    return v;
}

/* ---- the list of environments and functions, for rt_shutdown ---------- */

static ObjEnv *all_envs = NULL;
static ObjFunction *all_functions = NULL;
static bool shutting_down = false;

void rt_track_env(ObjEnv *env) {
    env->track_prev = NULL;
    env->track_next = all_envs;
    if (all_envs != NULL) all_envs->track_prev = env;
    all_envs = env;
}

void rt_untrack_env(ObjEnv *env) {
    if (env->track_prev != NULL) env->track_prev->track_next = env->track_next;
    else all_envs = env->track_next;
    if (env->track_next != NULL) env->track_next->track_prev = env->track_prev;
}

static void track_function(ObjFunction *f) {
    f->track_prev = NULL;
    f->track_next = all_functions;
    if (all_functions != NULL) all_functions->track_prev = f;
    all_functions = f;
}

static void untrack_function(ObjFunction *f) {
    if (f->track_prev != NULL) f->track_prev->track_next = f->track_next;
    else all_functions = f->track_next;
    if (f->track_next != NULL) f->track_next->track_prev = f->track_prev;
}

Value value_function(const char *name, const void *proto, ObjEnv *closure, int native) {
    ObjFunction *f = xcalloc(sizeof *f);
    f->obj.kind = OBJ_FUNCTION;
    f->obj.refcount = 1;
    f->name = name;
    f->proto = proto;
    f->closure = closure;
    f->native = native;
    track_function(f);
    Value v;
    v.kind = VAL_OBJ;
    v.as.obj = &f->obj;
    return v;
}

static void function_destroy(ObjFunction *f) {
    untrack_function(f);
    if (f->closure != NULL && !shutting_down) env_release(f->closure);
    free(f);
}

bool rt_shutting_down(void) {
    return shutting_down;
}

bool value_is(Value v, ObjKind kind) {
    return v.kind == VAL_OBJ && v.as.obj->kind == kind;
}

void value_retain(Value v) {
    if (v.kind == VAL_OBJ) v.as.obj->refcount++;
}

void value_release(Value v) {
    if (v.kind != VAL_OBJ) return;
    Obj *obj = v.as.obj;
    /* During shutdown the sweep below frees environments and functions
     * directly, cycles and all; a release reaching one of them must not
     * free it a second time. */
    if (shutting_down && (obj->kind == OBJ_ENV || obj->kind == OBJ_FUNCTION)) return;
    if (--obj->refcount != 0) return;
    switch (obj->kind) {
    case OBJ_STRING:
    case OBJ_RANGE:
        free(obj);
        break;
    case OBJ_FUNCTION:
        function_destroy((ObjFunction *)obj);
        break;
    case OBJ_ENV:
        env_destroy((ObjEnv *)obj);
        break;
    }
}

void rt_shutdown(void) {
    shutting_down = true;
    while (all_envs != NULL) env_destroy(all_envs);
    while (all_functions != NULL) function_destroy(all_functions);
    shutting_down = false;
}

const char *value_type_name(Value v) {
    switch (v.kind) {
    case VAL_NULL: return "null";
    case VAL_BOOL: return "bool";
    case VAL_INT: return "int";
    case VAL_FLOAT: return "float";
    case VAL_OBJ:
        switch (v.as.obj->kind) {
        case OBJ_STRING: return "string";
        case OBJ_RANGE: return "range";
        case OBJ_FUNCTION: return "fn";
        case OBJ_ENV: return "scope";
        }
    }
    return "?";
}

/* Floats print like Python's repr: the shortest digits that read back to
 * the same value; plain notation with a decimal point when the exponent
 * is between -4 and 15 (3.0, 0.0025, 123456789.123), exponent notation
 * otherwise (1e+16, 2.5e-05, 5e-324). */
static void print_float(double f, FILE *out) {
    if (isnan(f)) { fputs("nan", out); return; }
    if (isinf(f)) { fputs(f < 0 ? "-inf" : "inf", out); return; }
    if (f == 0.0) { fputs(signbit(f) ? "-0.0" : "0.0", out); return; }

    /* Shortest mantissa: the fewest significant digits that round-trip. */
    char buf[48];
    int digits = 1;
    for (; digits <= 17; digits++) {
        snprintf(buf, sizeof buf, "%.*e", digits - 1, f);
        if (strtod(buf, NULL) == f) break;
    }
    char *e = strchr(buf, 'e');
    int exponent = (int)strtol(e + 1, NULL, 10);

    if (exponent >= -4 && exponent < 16) {
        int decimals = digits - 1 - exponent;
        if (decimals < 0) decimals = 0;
        snprintf(buf, sizeof buf, "%.*f", decimals, f);
        fputs(buf, out);
        if (strchr(buf, '.') == NULL) fputs(".0", out);
        return;
    }
    /* Exponent form: mantissa without trailing zeros, exponent with a sign
     * and at least two digits, as Python writes it. */
    *e = '\0';
    char *dot = strchr(buf, '.');
    if (dot != NULL) {
        char *end = buf + strlen(buf);
        while (end > dot + 1 && end[-1] == '0') *--end = '\0';
        if (end == dot + 1) *dot = '\0';
    }
    fprintf(out, "%se%c%02d", buf, exponent < 0 ? '-' : '+', exponent < 0 ? -exponent : exponent);
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
    case VAL_OBJ:
        switch (v.as.obj->kind) {
        case OBJ_STRING: {
            ObjString *s = (ObjString *)v.as.obj;
            fwrite(s->chars, 1, s->length, out);
            break;
        }
        case OBJ_RANGE: {
            ObjRange *r = (ObjRange *)v.as.obj;
            fprintf(out, "%" PRId64 "%s%" PRId64, r->start, r->inclusive ? "..=" : "..", r->end);
            break;
        }
        case OBJ_FUNCTION:
            fprintf(out, "<fn %s>", ((ObjFunction *)v.as.obj)->name);
            break;
        case OBJ_ENV:
            fputs("<scope>", out);
            break;
        }
        break;
    }
}
