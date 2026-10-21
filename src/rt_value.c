#include "arete_rt.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

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

void value_retain(Value v) {
    if (v.kind == VAL_OBJ) v.as.obj->refcount++;
}

void value_release(Value v) {
    if (v.kind != VAL_OBJ) return;
    Obj *obj = v.as.obj;
    if (--obj->refcount == 0) free(obj);
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
    case VAL_OBJ: {
        ObjString *s = (ObjString *)v.as.obj;
        fwrite(s->chars, 1, s->length, out);
        break;
    }
    }
}
