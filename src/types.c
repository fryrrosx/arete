#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

void type_arena_init(TypeArena *a) {
    a->all = NULL;
    for (int t = 0; t <= TY_ANY; t++) {
        memset(&a->simple[t], 0, sizeof(Type));
        a->simple[t].tag = (TypeTag)t;
    }
}

void type_arena_free(TypeArena *a) {
    Type *t = a->all;
    while (t != NULL) {
        Type *next = t->next;
        free(t->params);
        free(t);
        t = next;
    }
    a->all = NULL;
}

Type *type_simple(TypeArena *a, TypeTag tag) {
    return &a->simple[tag];
}

Type *type_fn(TypeArena *a, Type **params, size_t count, Type *result) {
    Type *t = xcalloc(sizeof *t);
    t->tag = TY_FN;
    t->params = params;
    t->param_count = count;
    t->result = result;
    t->next = a->all;
    a->all = t;
    return t;
}

bool type_equal(const Type *a, const Type *b) {
    if (a == b) return true;
    if (a == NULL || b == NULL) return false;
    if (a->tag != b->tag) return false;
    if (a->tag != TY_FN) return true;
    if (a->param_count != b->param_count) return false;
    for (size_t i = 0; i < a->param_count; i++) {
        if (!type_equal(a->params[i], b->params[i])) return false;
    }
    if ((a->result == NULL) != (b->result == NULL)) return false;
    return a->result == NULL || type_equal(a->result, b->result);
}

bool type_accepts(const Type *to, const Type *from) {
    if (to->tag == TY_ANY) return from->tag != TY_NONE;
    if (to->tag == TY_FLOAT && from->tag == TY_INT) return true;   /* widening, never the reverse */
    if (to->tag == TY_FN && from->tag == TY_FN) {
        /* A function whose result is dropped may be passed where one that
         * returns nothing is expected; a built-in taking any value (print)
         * fits any parameter type. */
        if (to->param_count != from->param_count) return false;
        for (size_t i = 0; i < to->param_count; i++) {
            if (from->params[i]->tag == TY_ANY) continue;
            if (!type_equal(to->params[i], from->params[i])) return false;
        }
        if (to->result == NULL) return true;
        return from->result != NULL && type_equal(to->result, from->result);
    }
    return type_equal(to, from);
}

/* Appends `text`; when the buffer is full the name ends in "..." so that
 * a cut type never reads as another type. */
static size_t append(char *buf, size_t cap, size_t at, const char *text) {
    size_t n = strlen(text);
    if (at >= cap) return at;
    size_t room = cap - at - 1;
    if (n <= room) {
        memcpy(buf + at, text, n);
        buf[at + n] = '\0';
        return at + n;
    }
    memcpy(buf + at, text, room);
    if (cap > 4) memcpy(buf + cap - 4, "...", 3);      /* over whatever was written last */
    buf[cap - 1] = '\0';
    return cap;
}

static size_t write_type(const Type *t, char *buf, size_t cap, size_t at) {
    switch (t->tag) {
    case TY_INT: return append(buf, cap, at, "int");
    case TY_FLOAT: return append(buf, cap, at, "float");
    case TY_STRING: return append(buf, cap, at, "string");
    case TY_BOOL: return append(buf, cap, at, "bool");
    case TY_RANGE: return append(buf, cap, at, "range");
    case TY_NONE: return append(buf, cap, at, "nothing");
    case TY_ANY: return append(buf, cap, at, "any");
    case TY_FN:
        at = append(buf, cap, at, "fn(");
        for (size_t i = 0; i < t->param_count; i++) {
            if (i > 0) at = append(buf, cap, at, ", ");
            at = write_type(t->params[i], buf, cap, at);
        }
        at = append(buf, cap, at, ")");
        if (t->result != NULL) {
            at = append(buf, cap, at, " -> ");
            at = write_type(t->result, buf, cap, at);
        }
        return at;
    }
    return at;
}

void type_name(const Type *t, char *buf, size_t cap) {
    if (cap == 0) return;
    buf[0] = '\0';
    write_type(t, buf, cap, 0);
}

/* The phase in which a written type becomes real, for the message. */
static bool later_phase(const TypeExpr *te, const char *what, int phase, Diag *err) {
    char summary[160], help[160];
    snprintf(summary, sizeof summary, "%s arrive in phase %d", what, phase);
    snprintf(help, sizeof help, "the types so far are int, float, string, bool and fn(...) -> T");
    diag_set(err, te->line, te->column, summary, "this type", help);
    return false;
}

Type *type_from_expr(TypeArena *a, const TypeExpr *te, Diag *err) {
    switch (te->kind) {
    case TYPE_NULLABLE:
        later_phase(te, "nullable types like `int?`", 16, err);
        return NULL;
    case TYPE_UNION:
        later_phase(te, "union types like `int | string`", 13, err);
        return NULL;
    case TYPE_FN: {
        if (te->throws) {
            later_phase(te, "`throws` in a function type", 14, err);
            return NULL;
        }
        Type **params = te->item_count > 0 ? xmalloc(te->item_count * sizeof(Type *)) : NULL;
        for (size_t i = 0; i < te->item_count; i++) {
            params[i] = type_from_expr(a, te->items[i], err);
            if (params[i] == NULL) { free(params); return NULL; }
        }
        Type *result = NULL;
        if (te->inner != NULL) {
            result = type_from_expr(a, te->inner, err);
            if (result == NULL) { free(params); return NULL; }
        }
        return type_fn(a, params, te->item_count, result);
    }
    case TYPE_NAME: {
        const char *n = te->name;
        if (te->item_count > 0) {
            bool collection = strcmp(n, "List") == 0 || strcmp(n, "MutableList") == 0 ||
                              strcmp(n, "Map") == 0 || strcmp(n, "MutableMap") == 0 ||
                              strcmp(n, "Set") == 0 || strcmp(n, "MutableSet") == 0;
            later_phase(te, collection ? "collection types like `List<int>`" : "generic types like `Box<T>`",
                        collection ? 12 : 17, err);
            return NULL;
        }
        if (strcmp(n, "int") == 0) return type_simple(a, TY_INT);
        if (strcmp(n, "float") == 0) return type_simple(a, TY_FLOAT);
        if (strcmp(n, "string") == 0) return type_simple(a, TY_STRING);
        if (strcmp(n, "bool") == 0) return type_simple(a, TY_BOOL);
        if (strcmp(n, "range") == 0) return type_simple(a, TY_RANGE);
        char summary[160];
        snprintf(summary, sizeof summary, "unknown type `%s`", n);
        diag_set(err, te->line, te->column, summary, "no type has this name",
                 "the types so far are int, float, string, bool and fn(...) -> T");
        return NULL;
    }
    }
    return NULL;
}
