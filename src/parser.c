#include "parser.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/* Deepest expression the parser accepts, counting both nesting and the
 * length of one operator chain. Keeps every later recursive walk over the
 * tree (check, eval, dump, free) safely inside the C stack. */
#define MAX_EXPR_DEPTH 1000

typedef struct {
    TokenList *tokens;
    size_t pos;
    bool repl;
    int depth;         /* current expression nesting */
    const Source *src;
    Diag *err;
} Parser;

/* ---- helpers ---------------------------------------------------------- */

static Token *peek(const Parser *p) {
    return &p->tokens->items[p->pos];
}

static Token *peek_at(const Parser *p, size_t ahead) {
    size_t i = p->pos + ahead;
    if (i >= p->tokens->count) i = p->tokens->count - 1;   /* the EOF token */
    return &p->tokens->items[i];
}

static Token *advance(Parser *p) {
    Token *t = peek(p);
    if (t->kind != TOK_EOF) p->pos++;
    return t;
}

static bool peek_is(const Parser *p, TokenKind kind) {
    return peek(p)->kind == kind;
}

static void error_at(Parser *p, const Token *t, const char *summary,
                     const char *label, const char *help) {
    diag_set(p->err, t->line, t->column, summary, label, help);
}

/* Consumes a token of `kind` or reports "expected X, found Y". */
static bool expect(Parser *p, TokenKind kind, const char *what, const char *help) {
    Token *t = peek(p);
    if (t->kind == kind) {
        advance(p);
        return true;
    }
    char summary[160];
    snprintf(summary, sizeof summary, "expected %s, found %s",
             what, token_kind_describe(t->kind));
    if (t->kind == TOK_PIPE) help = "`|` only separates types; for logic write `or`";
    error_at(p, t, summary, "here", help);
    return false;
}

static bool reserved_word_error(Parser *p, const Token *t) {
    char summary[120];
    snprintf(summary, sizeof summary, "`%s` is a reserved word", t->text);
    error_at(p, t, summary, "cannot be used as a name",
             "it becomes a keyword in a later phase; choose another name");
    return false;
}

/* ---- literals --------------------------------------------------------- */

/* The literal's text without underscores, malloc'd. */
static char *strip_underscores(const Token *t) {
    char *clean = xmalloc(t->length + 1);
    size_t n = 0;
    for (size_t i = 0; i < t->length; i++) {
        if (t->text[i] != '_') clean[n++] = t->text[i];
    }
    clean[n] = '\0';
    return clean;
}

/* Converts the text of an INT token (any base) with an overflow check. */
static bool parse_int_literal(Parser *p, const Token *t, int64_t *out) {
    char *clean = strip_underscores(t);
    int base = 10;
    const char *digits = clean;
    if (clean[0] == '0' && (clean[1] == 'x' || clean[1] == 'X')) { base = 16; digits = clean + 2; }
    if (clean[0] == '0' && (clean[1] == 'b' || clean[1] == 'B')) { base = 2;  digits = clean + 2; }
    errno = 0;
    unsigned long long v = strtoull(digits, NULL, base);
    bool too_large = (errno == ERANGE || v > (unsigned long long)INT64_MAX);
    free(clean);
    if (too_large) {
        error_at(p, t, "integer literal is too large", "does not fit in 64 bits",
                 "the largest int is 9223372036854775807; "
                 "the smallest is written -9223372036854775807 - 1");
        return false;
    }
    *out = (int64_t)v;
    return true;
}

static bool parse_float_literal(Parser *p, const Token *t, double *out) {
    char *clean = strip_underscores(t);
    errno = 0;
    double v = strtod(clean, NULL);
    bool too_large = (errno == ERANGE && isinf(v));
    free(clean);
    if (too_large) {
        error_at(p, t, "float literal is too large", "does not fit in a 64-bit float", NULL);
        return false;
    }
    *out = v;
    return true;
}

/* ---- types (GRAMMAR.md: type, union_type, postfix_type, primary_type) ---- */

static TypeExpr *parse_type(Parser *p);

static void type_list_add(TypeExpr *t, TypeExpr *item) {
    t->items = xrealloc(t->items, (t->item_count + 1) * sizeof(TypeExpr *));
    t->items[t->item_count++] = item;
}

/* Expects `>` in a type. `List<int>=[]` without spaces lexes as `>=`, so
 * that token is split: one `>` is consumed and `=` stays for the caller. */
static bool expect_close_angle(Parser *p) {
    Token *t = peek(p);
    if (t->kind == TOK_GREATER) {
        advance(p);
        return true;
    }
    if (t->kind == TOK_GREATER_EQUAL) {
        t->kind = TOK_EQUAL;
        t->column++;
        return true;
    }
    return expect(p, TOK_GREATER, "`>`", "type arguments end with `>`");
}

/* The "fn" alternative of primary_type:
 *   "fn" "(" [ type { "," type } ] ")" [ "->" type ] [ "throws" [ IDENT ] ] */
static TypeExpr *parse_fn_type(Parser *p) {
    Token *kw = advance(p);
    TypeExpr *ty = type_new(TYPE_FN, kw->line, kw->column);
    if (!expect(p, TOK_LPAREN, "`(`", "a function type looks like fn(int) -> string")) {
        type_free(ty);
        return NULL;
    }
    if (!peek_is(p, TOK_RPAREN)) {
        for (;;) {
            TypeExpr *param = parse_type(p);
            if (param == NULL) { type_free(ty); return NULL; }
            type_list_add(ty, param);
            if (peek_is(p, TOK_COMMA)) { advance(p); continue; }
            break;
        }
    }
    if (!expect(p, TOK_RPAREN, "`)`", NULL)) { type_free(ty); return NULL; }
    if (peek_is(p, TOK_ARROW)) {
        advance(p);
        ty->inner = parse_type(p);
        if (ty->inner == NULL) { type_free(ty); return NULL; }
    }
    if (peek_is(p, TOK_THROWS)) {
        advance(p);
        ty->throws = true;
        if (peek_is(p, TOK_IDENT)) ty->throws_name = xstrdup(advance(p)->text);
    }
    return ty;
}

/* primary_type = IDENT [ "<" type { "," type } ">" ] | fn_type | "(" type ")" ; */
static TypeExpr *parse_primary_type(Parser *p) {
    Token *t = peek(p);
    if (t->kind == TOK_IDENT) {
        advance(p);
        TypeExpr *ty = type_new(TYPE_NAME, t->line, t->column);
        ty->name = xstrdup(t->text);
        if (peek_is(p, TOK_LESS)) {
            advance(p);
            for (;;) {
                TypeExpr *arg = parse_type(p);
                if (arg == NULL) { type_free(ty); return NULL; }
                type_list_add(ty, arg);
                if (peek_is(p, TOK_COMMA)) { advance(p); continue; }
                break;
            }
            if (!expect_close_angle(p)) { type_free(ty); return NULL; }
        }
        return ty;
    }
    if (t->kind == TOK_FN) return parse_fn_type(p);
    if (t->kind == TOK_LPAREN) {
        advance(p);
        TypeExpr *inner = parse_type(p);
        if (inner == NULL) return NULL;
        if (!expect(p, TOK_RPAREN, "`)`", NULL)) { type_free(inner); return NULL; }
        return inner;
    }
    if (t->kind == TOK_RESERVED && strcmp(t->text, "null") == 0) {
        error_at(p, t, "null is not a type name", "here",
                 "write the nullable type with `?`, for example `int?`");
        return NULL;
    }
    char summary[160];
    snprintf(summary, sizeof summary, "expected a type, found %s", token_kind_describe(t->kind));
    error_at(p, t, summary, "here", "for example int, string, List<int>, int?");
    return NULL;
}

/* postfix_type = primary_type { "?" } ; */
static TypeExpr *parse_postfix_type(Parser *p) {
    TypeExpr *ty = parse_primary_type(p);
    while (ty != NULL && peek_is(p, TOK_QUESTION)) {
        Token *q = advance(p);
        TypeExpr *wrapped = type_new(TYPE_NULLABLE, q->line, q->column);
        wrapped->inner = ty;
        ty = wrapped;
    }
    return ty;
}

/* union_type = postfix_type { "|" postfix_type } ;  type = union_type ; */
static TypeExpr *parse_type(Parser *p) {
    TypeExpr *first = parse_postfix_type(p);
    if (first == NULL || !peek_is(p, TOK_PIPE)) return first;
    TypeExpr *u = type_new(TYPE_UNION, first->line, first->column);
    type_list_add(u, first);
    while (peek_is(p, TOK_PIPE)) {
        advance(p);
        TypeExpr *next = parse_postfix_type(p);
        if (next == NULL) { type_free(u); return NULL; }
        type_list_add(u, next);
    }
    return u;
}

/* ---- expressions ------------------------------------------------------ */

static Expr *parse_expression(Parser *p);
static Expr *parse_binary(Parser *p, int min_prec);

/* Precedence levels from SPEC.md section 3 (higher binds tighter). */
enum {
    PREC_OR = 3,
    PREC_AND = 4,
    PREC_NOT = 5,
    PREC_COMPARE = 6,
    PREC_RANGE = 8,
    PREC_ADD = 9,
    PREC_MUL = 10,
    PREC_UNARY = 11,
    PREC_POWER = 12
};

typedef struct { TokenKind op; int prec; bool right_assoc; bool logical; } InfixRule;

static const InfixRule INFIX[] = {
    { TOK_OR, PREC_OR, false, true },
    { TOK_AND, PREC_AND, false, true },
    { TOK_EQUAL_EQUAL, PREC_COMPARE, false, false },
    { TOK_BANG_EQUAL, PREC_COMPARE, false, false },
    { TOK_LESS, PREC_COMPARE, false, false },
    { TOK_LESS_EQUAL, PREC_COMPARE, false, false },
    { TOK_GREATER, PREC_COMPARE, false, false },
    { TOK_GREATER_EQUAL, PREC_COMPARE, false, false },
    { TOK_DOT_DOT, PREC_RANGE, false, false },
    { TOK_DOT_DOT_EQUAL, PREC_RANGE, false, false },
    { TOK_PLUS, PREC_ADD, false, false },
    { TOK_MINUS, PREC_ADD, false, false },
    { TOK_STAR, PREC_MUL, false, false },
    { TOK_SLASH, PREC_MUL, false, false },
    { TOK_SLASH_SLASH, PREC_MUL, false, false },
    { TOK_STAR_STAR, PREC_POWER, true, false },
};

static const InfixRule *infix_rule(TokenKind kind) {
    for (size_t i = 0; i < sizeof INFIX / sizeof INFIX[0]; i++) {
        if (INFIX[i].op == kind) return &INFIX[i];
    }
    return NULL;
}

static bool too_deep(Parser *p, const Token *at) {
    error_at(p, at, "expression nested too deeply", "here",
             "split it into smaller steps with `let`");
    return true;
}

static const char *literal_kind_name(const Expr *e) {
    switch (e->kind) {
    case EXPR_INT: return "an int";
    case EXPR_FLOAT: return "a float";
    case EXPR_STRING: return "a string";
    case EXPR_BOOL: return "a bool";
    default: return NULL;
    }
}

/* argument  = [ IDENT "=" ] expression ;
 * arguments = argument { "," argument } ;
 * call      = "(" [ arguments ] ")" ;
 * The "(" has been seen by the caller, not consumed. Named arguments
 * follow the positional ones, and a name is given at most once. */
static Expr *parse_call(Parser *p, Expr *callee) {
    Token *open = advance(p);                       /* the "(" */
    const char *literal = literal_kind_name(callee);
    if (literal != NULL) {
        char summary[80], label[80];
        snprintf(summary, sizeof summary, "%s cannot be called", literal);
        snprintf(label, sizeof label, "this is %s, not a function", literal);
        error_at(p, open, summary, label, "to call a function write its name: f(...)");
        expr_free(callee);
        return NULL;
    }
    Expr **args = NULL;
    char **names = NULL;
    size_t count = 0, capacity = 0;
    bool seen_named = false;
    if (!peek_is(p, TOK_RPAREN)) {
        for (;;) {
            char *name = NULL;
            Token *first = peek(p);
            if (first->kind == TOK_IDENT && peek_at(p, 1)->kind == TOK_EQUAL) {
                for (size_t i = 0; i < count; i++) {
                    if (names[i] != NULL && strcmp(names[i], first->text) == 0) {
                        char summary[160];
                        snprintf(summary, sizeof summary, "argument `%s` is given twice", first->text);
                        error_at(p, first, summary, "second time here", NULL);
                        goto fail;
                    }
                }
                name = xstrdup(first->text);
                advance(p);
                advance(p);
                seen_named = true;
            } else if (seen_named) {
                error_at(p, first, "positional argument after a named one", "here",
                         "named arguments go last: f(1, 2, name = 3)");
                goto fail;
            }
            Expr *arg = parse_expression(p);
            if (arg == NULL) { free(name); goto fail; }
            if (count == capacity) {
                capacity = capacity == 0 ? 4 : capacity * 2;
                args = xrealloc(args, capacity * sizeof(Expr *));
                names = xrealloc(names, capacity * sizeof(char *));
            }
            args[count] = arg;
            names[count] = name;
            count++;
            if (peek_is(p, TOK_COMMA)) {
                advance(p);
                if (peek_is(p, TOK_RPAREN)) {
                    error_at(p, peek(p), "expected an argument after `,`, found `)`", "here",
                             "remove the trailing comma");
                    goto fail;
                }
                continue;
            }
            break;
        }
    }
    if (!expect(p, TOK_RPAREN, "`)`", "arguments are separated by `,`")) goto fail;

    Expr *call = expr_new(EXPR_CALL, callee->line, callee->column);
    call->as.call.callee = callee;
    call->as.call.args = args;
    call->as.call.names = names;
    call->as.call.arg_count = count;
    return call;

fail:
    for (size_t i = 0; i < count; i++) {
        expr_free(args[i]);
        free(names[i]);
    }
    free(args);
    free(names);
    expr_free(callee);
    return NULL;
}

/* params = param { "," param } ;
 * param  = IDENT ":" type [ "=" expression ]      in a fn declaration
 *        | IDENT [ ":" type ]                      in a lambda
 * The "(" is consumed by the caller; this stops before ")". */
static bool parse_params(Parser *p, FnProto *f, bool is_lambda) {
    if (peek_is(p, TOK_RPAREN)) return true;
    for (;;) {
        Token *name = peek(p);
        if (name->kind == TOK_RESERVED) return reserved_word_error(p, name);
        if (name->kind != TOK_IDENT) {
            char summary[160];
            snprintf(summary, sizeof summary, "expected a parameter name, found %s",
                     token_kind_describe(name->kind));
            error_at(p, name, summary, "here", "parameters are written `name: type`");
            return false;
        }
        advance(p);
        for (size_t i = 0; i < f->param_count; i++) {
            if (strcmp(name->text, "_") != 0 && strcmp(f->params[i].name, name->text) == 0) {
                char summary[160];
                snprintf(summary, sizeof summary, "parameter `%s` is declared twice", name->text);
                error_at(p, name, summary, "second time here", NULL);
                return false;
            }
        }
        Param param = {0};
        param.name = xstrdup(name->text);
        param.line = name->line;
        param.column = name->column;
        fn_proto_add_param(f, param);
        Param *slot = &f->params[f->param_count - 1];
        if (peek_is(p, TOK_COLON)) {
            advance(p);
            slot->type = parse_type(p);
            if (slot->type == NULL) return false;
        } else if (!is_lambda) {
            char summary[160], help[160];
            snprintf(summary, sizeof summary, "parameter `%s` needs a type", name->text);
            snprintf(help, sizeof help, "write `%s: int` (or another type)", name->text);
            error_at(p, peek(p), summary, "expected `:` and a type here", help);
            return false;
        }
        if (peek_is(p, TOK_EQUAL)) {
            Token *eq = advance(p);
            if (is_lambda) {
                error_at(p, eq, "a lambda parameter cannot have a default value", "here",
                         "declare a named function with `fn name(...)` instead");
                return false;
            }
            if (peek_is(p, TOK_RPAREN) || peek_is(p, TOK_COMMA)) {
                char summary[160], help[160];
                snprintf(summary, sizeof summary, "expected a default value, found %s",
                         token_kind_describe(peek(p)->kind));
                snprintf(help, sizeof help, "give the default a value: `%s: int = 0`", name->text);
                error_at(p, peek(p), summary, "here", help);
                return false;
            }
            slot->default_value = parse_expression(p);
            if (slot->default_value == NULL) return false;
        }
        if (peek_is(p, TOK_COMMA)) {
            advance(p);
            if (peek_is(p, TOK_RPAREN)) {
                error_at(p, peek(p), "expected a parameter after `,`, found `)`", "here",
                         "remove the trailing comma");
                return false;
            }
            continue;
        }
        return true;
    }
}

/* lambda = "fn" "(" [ params ] ")" [ "->" type ] "=>" expression ;
 * The body extends as far right as possible (level 1). */
static Expr *parse_lambda(Parser *p) {
    Token *kw = advance(p);
    if (peek_is(p, TOK_IDENT)) {
        error_at(p, peek(p), "a named function cannot be declared inside an expression",
                 "the name here", "declare it on its own line: `fn name(...):`, "
                 "or drop the name for a lambda: `fn(x) => x * 2`");
        return NULL;
    }
    FnProto *f = fn_proto_new(kw->line, kw->column);
    f->source = p->src;
    if (!expect(p, TOK_LPAREN, "`(`", "a lambda is written `fn(x) => x * 2`")) goto fail;
    if (!parse_params(p, f, true)) goto fail;
    if (!expect(p, TOK_RPAREN, "`)`", "parameters are separated by `,`")) goto fail;
    if (peek_is(p, TOK_ARROW)) {
        advance(p);
        f->result = parse_type(p);
        if (f->result == NULL) goto fail;
    }
    if (!expect(p, TOK_FAT_ARROW, "`=>`", "a lambda is written `fn(x) => x * 2`; "
                "a function with a block body is declared with `fn name(x: int):`")) goto fail;
    switch (peek(p)->kind) {
    case TOK_NEWLINE: case TOK_EOF: case TOK_RETURN: case TOK_BREAK: case TOK_CONTINUE:
    case TOK_LET: case TOK_VAR: case TOK_WHILE: case TOK_FOR: case TOK_IF: {
        char summary[160];
        snprintf(summary, sizeof summary, "expected an expression after `=>`, found %s",
                 token_kind_describe(peek(p)->kind));
        error_at(p, peek(p), summary, "a lambda body is one expression",
                 "write `fn(x) => x * 2`; for statements declare a named function: `fn name(...):`");
        goto fail;
    }
    default:
        break;
    }
    f->expr_body = parse_expression(p);
    if (f->expr_body == NULL) goto fail;
    Expr *e = expr_new(EXPR_LAMBDA, kw->line, kw->column);
    e->as.lambda.proto = f;
    return e;

fail:
    fn_proto_free(f);
    return NULL;
}

/* primary = INT | FLOAT | STRING | "true" | "false" | IDENT | "(" expression ")" | lambda ; */
static Expr *parse_primary(Parser *p) {
    Token *t = peek(p);
    Expr *e = NULL;
    switch (t->kind) {
    case TOK_INT: {
        int64_t v;
        if (!parse_int_literal(p, t, &v)) return NULL;
        advance(p);
        e = expr_new(EXPR_INT, t->line, t->column);
        e->as.integer = v;
        return e;
    }
    case TOK_FLOAT: {
        double v;
        if (!parse_float_literal(p, t, &v)) return NULL;
        advance(p);
        e = expr_new(EXPR_FLOAT, t->line, t->column);
        e->as.number = v;
        return e;
    }
    case TOK_STRING:
        advance(p);
        e = expr_new(EXPR_STRING, t->line, t->column);
        e->as.string.chars = xstrndup(t->text, t->length);
        e->as.string.length = t->length;
        return e;
    case TOK_TRUE:
    case TOK_FALSE:
        advance(p);
        e = expr_new(EXPR_BOOL, t->line, t->column);
        e->as.boolean = (t->kind == TOK_TRUE);
        return e;
    case TOK_IDENT:
        advance(p);
        e = expr_new(EXPR_NAME, t->line, t->column);
        e->as.name.name = xstrdup(t->text);
        return e;
    case TOK_LPAREN: {
        advance(p);
        Expr *inner = parse_expression(p);
        if (inner == NULL) return NULL;
        if (!expect(p, TOK_RPAREN, "`)`", NULL)) {
            expr_free(inner);
            return NULL;
        }
        return inner;
    }
    case TOK_FN:
        return parse_lambda(p);
    case TOK_RESERVED:
        reserved_word_error(p, t);
        return NULL;
    case TOK_LBRACKET:
        error_at(p, t, "lists arrive in phase 12", "a list here", NULL);
        return NULL;
    default: {
        char summary[160];
        snprintf(summary, sizeof summary, "expected an expression, found %s",
                 token_kind_describe(t->kind));
        error_at(p, t, summary, "here", NULL);
        return NULL;
    }
    }
}

/* postfix = primary { call } ; */
static Expr *parse_postfix(Parser *p) {
    Expr *e = parse_primary(p);
    while (e != NULL && peek_is(p, TOK_LPAREN)) e = parse_call(p, e);
    return e;
}

/* Prefix operators: `not` (level 5) and unary `-` (level 11). The operand
 * of `-` is parsed at level 11, so `-2 ** 2` is -(2 ** 2) and `-7 // 2`
 * is (-7) // 2, as SPEC.md section 3 requires. `not` binds looser than
 * every arithmetic and comparison operator, so it cannot be their operand:
 * `1 == not x` is an error, as in the grammar. */
static Expr *parse_prefix(Parser *p, int min_prec) {
    Token *t = peek(p);
    if (t->kind == TOK_NOT && min_prec > PREC_NOT) {
        error_at(p, t, "`not` cannot be an operand here", "`not` binds looser than this operator",
                 "add parentheses: `a == (not b)`");
        return NULL;
    }
    if (t->kind == TOK_NOT || t->kind == TOK_MINUS) {
        advance(p);
        Expr *operand = parse_binary(p, t->kind == TOK_NOT ? PREC_NOT : PREC_UNARY);
        if (operand == NULL) return NULL;
        Expr *e = expr_new(EXPR_UNARY, t->line, t->column);
        e->as.unary.op = t->kind;
        e->as.unary.operand = operand;
        return e;
    }
    return parse_postfix(p);
}

/* The Pratt loop: binary operators by precedence (GRAMMAR.md: additive,
 * multiplicative, power, comparison, and, or). */
static Expr *parse_binary_inner(Parser *p, int min_prec) {
    Expr *left = parse_prefix(p, min_prec);
    int chain = 0;                       /* operators in a row at this level */
    while (left != NULL) {
        Token *t = peek(p);
        const InfixRule *rule = infix_rule(t->kind);
        if (rule == NULL || rule->prec < min_prec) break;
        if (p->depth + ++chain > MAX_EXPR_DEPTH) {
            too_deep(p, t);
            expr_free(left);
            return NULL;
        }
        advance(p);
        Expr *right = parse_binary(p, rule->right_assoc ? rule->prec : rule->prec + 1);
        if (right == NULL) {
            expr_free(left);
            return NULL;
        }
        Expr *e;
        if (rule->prec == PREC_RANGE) {
            e = expr_new(EXPR_RANGE, t->line, t->column);
            e->as.range.start = left;
            e->as.range.end = right;
            e->as.range.inclusive = (t->kind == TOK_DOT_DOT_EQUAL);
        } else {
            e = expr_new(rule->logical ? EXPR_LOGICAL : EXPR_BINARY, t->line, t->column);
            e->as.binary.op = t->kind;
            e->as.binary.left = left;
            e->as.binary.right = right;
        }
        left = e;
        if (rule->prec == PREC_RANGE) {
            const InfixRule *next = infix_rule(peek(p)->kind);
            if (next != NULL && next->prec == PREC_RANGE) {
                error_at(p, peek(p), "a range cannot be an end of another range", "second range operator here",
                         "a range is `start..end` with a number on each side");
                expr_free(left);
                return NULL;
            }
        }
        if (rule->prec == PREC_COMPARE) {
            const InfixRule *next = infix_rule(peek(p)->kind);
            if (next != NULL && next->prec == PREC_COMPARE) {
                error_at(p, peek(p), "chained comparison is not allowed", "second comparison here",
                         "write `a < b and b < c`");
                expr_free(left);
                return NULL;
            }
        }
    }
    return left;
}

static Expr *parse_binary(Parser *p, int min_prec) {
    if (++p->depth > MAX_EXPR_DEPTH) {
        too_deep(p, peek(p));
        p->depth--;
        return NULL;
    }
    Expr *e = parse_binary_inner(p, min_prec);
    p->depth--;
    return e;
}

/* ternary = or [ "if" or "else" expression ] ;   expression = ternary ; */
static Expr *parse_expression(Parser *p) {
    Expr *value = parse_binary(p, PREC_OR);
    if (value == NULL || !peek_is(p, TOK_IF)) return value;
    Token *kw = advance(p);
    Expr *cond = parse_binary(p, PREC_OR);
    if (cond == NULL) {
        expr_free(value);
        return NULL;
    }
    if (peek_is(p, TOK_IF)) {
        error_at(p, peek(p), "the condition of a conditional expression cannot itself be conditional",
                 "second `if` here", "put it in parentheses: `a if (b if c else d) else e`");
        expr_free(value);
        expr_free(cond);
        return NULL;
    }
    if (!peek_is(p, TOK_ELSE)) {
        error_at(p, peek(p), "conditional expression needs else", "expected `else` here",
                 "write `a if condition else b`");
        expr_free(value);
        expr_free(cond);
        return NULL;
    }
    advance(p);
    Expr *otherwise = parse_expression(p);
    if (otherwise == NULL) {
        expr_free(value);
        expr_free(cond);
        return NULL;
    }
    Expr *e = expr_new(EXPR_TERNARY, kw->line, kw->column);
    e->as.ternary.cond = cond;
    e->as.ternary.then = value;
    e->as.ternary.otherwise = otherwise;
    return e;
}

/* ---- statements ------------------------------------------------------- */

static Stmt *parse_statement(Parser *p);

/* Ends a simple statement: NEWLINE (or EOF, for the last line of a REPL
 * input without a line end). */
static bool expect_end_of_line(Parser *p, const char *help) {
    if (peek_is(p, TOK_EOF)) return true;
    return expect(p, TOK_NEWLINE, "end of line", help);
}

/* block = NEWLINE INDENT statement { statement } DEDENT ; */
static StmtList *parse_block(Parser *p, const char *opener) {
    char help[160];
    snprintf(help, sizeof help,
             "the lines of the `%s` body go below it, indented by 4 spaces", opener);
    if (!expect(p, TOK_NEWLINE, "end of line", "the body starts on the next line")) return NULL;
    if (!peek_is(p, TOK_INDENT)) {
        char summary[96];
        snprintf(summary, sizeof summary, "expected an indented block after `%s`", opener);
        error_at(p, peek(p), summary, "this line is not indented", help);
        return NULL;
    }
    advance(p);
    StmtList *block = stmt_list_new();
    while (!peek_is(p, TOK_DEDENT) && !peek_is(p, TOK_EOF)) {
        Stmt *s = parse_statement(p);
        if (s == NULL) {
            stmt_list_free(block);
            return NULL;
        }
        stmt_list_add(block, s);
    }
    if (peek_is(p, TOK_DEDENT)) advance(p);
    return block;
}

/* if_stmt = "if" expression ":" block { "elif" expression ":" block } [ "else" ":" block ] ; */
static Stmt *parse_if(Parser *p) {
    Token *kw = advance(p);
    Stmt *s = stmt_new(STMT_IF, kw->line, kw->column);
    for (;;) {
        const char *word = (s->as.if_.count == 0) ? "if" : "elif";
        Expr *cond = parse_expression(p);
        if (cond == NULL) goto fail;
        char help[64];
        snprintf(help, sizeof help, "write `%s condition:`", word);
        if (!expect(p, TOK_COLON, "`:`", help)) { expr_free(cond); goto fail; }
        StmtList *block = parse_block(p, word);
        if (block == NULL) { expr_free(cond); goto fail; }

        size_t n = s->as.if_.count;
        s->as.if_.conds = xrealloc(s->as.if_.conds, (n + 1) * sizeof(Expr *));
        s->as.if_.blocks = xrealloc(s->as.if_.blocks, (n + 1) * sizeof(StmtList *));
        s->as.if_.conds[n] = cond;
        s->as.if_.blocks[n] = block;
        s->as.if_.count = n + 1;

        if (peek_is(p, TOK_ELIF)) {
            advance(p);
            continue;
        }
        break;
    }
    if (peek_is(p, TOK_ELSE)) {
        advance(p);
        if (peek_is(p, TOK_NEWLINE) || peek_is(p, TOK_EOF)) {
            expect(p, TOK_COLON, "`:`", "write `else:`");
            goto fail;
        }
        if (!peek_is(p, TOK_COLON)) {
            error_at(p, peek(p), "`else` takes no condition", "expected `:` right after `else`",
                     "write `else:`, or `elif condition:` for another test");
            goto fail;
        }
        advance(p);
        s->as.if_.otherwise = parse_block(p, "else");
        if (s->as.if_.otherwise == NULL) goto fail;
    }
    return s;

fail:
    stmt_free(s);
    return NULL;
}

/* while_stmt = "while" expression ":" block ; */
static Stmt *parse_while(Parser *p) {
    Token *kw = advance(p);
    Stmt *s = stmt_new(STMT_WHILE, kw->line, kw->column);
    s->as.while_.cond = parse_expression(p);
    if (s->as.while_.cond == NULL) goto fail;
    if (!expect(p, TOK_COLON, "`:`", "write `while condition:`")) goto fail;
    s->as.while_.body = parse_block(p, "while");
    if (s->as.while_.body == NULL) goto fail;
    return s;
fail:
    stmt_free(s);
    return NULL;
}

/* for_stmt = "for" IDENT "in" expression ":" block ; */
static Stmt *parse_for(Parser *p) {
    Token *kw = advance(p);
    Stmt *s = stmt_new(STMT_FOR, kw->line, kw->column);
    Token *name = peek(p);
    if (name->kind == TOK_RESERVED) { reserved_word_error(p, name); goto fail; }
    if (name->kind != TOK_IDENT) {
        char summary[160];
        snprintf(summary, sizeof summary, "expected a loop variable after `for`, found %s",
                 token_kind_describe(name->kind));
        error_at(p, name, summary, "here", "write `for x in 0..10:`");
        goto fail;
    }
    advance(p);
    s->as.for_.name = xstrdup(name->text);
    s->as.for_.name_line = name->line;
    s->as.for_.name_column = name->column;
    if (!expect(p, TOK_IN, "`in`", "write `for x in 0..10:`")) goto fail;
    s->as.for_.iterable = parse_expression(p);
    if (s->as.for_.iterable == NULL) goto fail;
    if (!expect(p, TOK_COLON, "`:`", "write `for x in 0..10:`")) goto fail;
    s->as.for_.body = parse_block(p, "for");
    if (s->as.for_.body == NULL) goto fail;
    return s;
fail:
    stmt_free(s);
    return NULL;
}

/* fn_decl = "fn" IDENT "(" [ params ] ")" [ "->" type ] ":" block ; */
static Stmt *parse_fn_decl(Parser *p) {
    Token *kw = advance(p);
    Token *name = advance(p);                       /* parse_statement saw the IDENT */
    Stmt *s = stmt_new(STMT_FN, kw->line, kw->column);
    FnProto *f = fn_proto_new(kw->line, kw->column);
    s->as.fn.proto = f;
    f->source = p->src;
    f->name = xstrdup(name->text);
    char help[160];
    snprintf(help, sizeof help, "write `fn %s(x: int) -> int:`", name->text);
    if (!expect(p, TOK_LPAREN, "`(`", help)) goto fail;
    if (!parse_params(p, f, false)) goto fail;
    if (!expect(p, TOK_RPAREN, "`)`", "parameters are separated by `,`")) goto fail;
    if (peek_is(p, TOK_ARROW)) {
        advance(p);
        f->result = parse_type(p);
        if (f->result == NULL) goto fail;
    }
    if (peek_is(p, TOK_FAT_ARROW)) {
        error_at(p, peek(p), "a named function has a block body", "`=>` here",
                 "write `fn name(...) -> type:` and the body below, indented; "
                 "`=>` is for lambdas: `let f = fn(x) => x * 2`");
        goto fail;
    }
    if (!expect(p, TOK_COLON, "`:`", help)) goto fail;
    f->body = parse_block(p, "fn");
    if (f->body == NULL) goto fail;
    return s;
fail:
    stmt_free(s);
    return NULL;
}

/* return_stmt = "return" [ expression ] NEWLINE ; */
static Stmt *parse_return(Parser *p) {
    Token *kw = advance(p);
    Stmt *s = stmt_new(STMT_RETURN, kw->line, kw->column);
    if (!peek_is(p, TOK_NEWLINE) && !peek_is(p, TOK_EOF)) {
        s->as.return_.value = parse_expression(p);
        if (s->as.return_.value == NULL) goto fail;
    }
    if (!expect_end_of_line(p, "`return` takes one expression")) goto fail;
    return s;
fail:
    stmt_free(s);
    return NULL;
}

/* break_stmt = "break" NEWLINE ;  continue_stmt = "continue" NEWLINE ; */
static Stmt *parse_jump(Parser *p, StmtKind kind) {
    Token *kw = advance(p);
    Stmt *s = stmt_new(kind, kw->line, kw->column);
    char help[80];
    snprintf(help, sizeof help, "`%s` stands alone on its line", kind == STMT_BREAK ? "break" : "continue");
    if (!expect_end_of_line(p, help)) {
        stmt_free(s);
        return NULL;
    }
    return s;
}

/* let_stmt = ( "let" | "var" ) IDENT [ ":" type ] "=" expression NEWLINE ; */
static Stmt *parse_let(Parser *p) {
    Token *kw = advance(p);
    const char *word = kw->kind == TOK_VAR ? "var" : "let";
    Stmt *s = stmt_new(STMT_LET, kw->line, kw->column);
    s->as.let.is_mutable = (kw->kind == TOK_VAR);
    Token *name = peek(p);
    if (name->kind == TOK_RESERVED) { reserved_word_error(p, name); goto fail; }
    char help[120];
    snprintf(help, sizeof help, "write `%s name = value`", word);
    if (!expect(p, TOK_IDENT, "a name", help)) goto fail;
    s->as.let.name = xstrdup(name->text);
    if (peek_is(p, TOK_COLON)) {
        advance(p);
        s->as.let.type = parse_type(p);
        if (s->as.let.type == NULL) goto fail;
    }
    if (!peek_is(p, TOK_EQUAL)) {
        char summary[120];
        if (peek_is(p, TOK_NEWLINE) || peek_is(p, TOK_EOF)) {
            snprintf(summary, sizeof summary, "`%s` needs a value", word);
            snprintf(help, sizeof help, "write `%s %s = value`", word, name->text);
            error_at(p, peek(p), summary, "expected `=` here", help);
        } else {
            snprintf(summary, sizeof summary, "expected `=` or `:`, found %s",
                     token_kind_describe(peek(p)->kind));
            snprintf(help, sizeof help, "a type annotation is written `%s %s: int = 5`", word, name->text);
            error_at(p, peek(p), summary, "here", help);
        }
        goto fail;
    }
    advance(p);
    s->as.let.value = parse_expression(p);
    if (s->as.let.value == NULL) goto fail;
    if (!expect_end_of_line(p, NULL)) goto fail;
    return s;

fail:
    stmt_free(s);
    return NULL;
}

static bool is_assign_op(TokenKind k) {
    return k == TOK_EQUAL || k == TOK_PLUS_EQUAL || k == TOK_MINUS_EQUAL
        || k == TOK_STAR_EQUAL || k == TOK_SLASH_EQUAL || k == TOK_SLASH_SLASH_EQUAL;
}

/* assign_stmt = IDENT assign_op expression NEWLINE ; */
static Stmt *parse_assign(Parser *p) {
    Token *name = advance(p);
    Token *op = advance(p);
    Stmt *s = stmt_new(STMT_ASSIGN, name->line, name->column);
    s->as.assign.name = xstrdup(name->text);
    s->as.assign.op = op->kind;
    s->as.assign.value = parse_expression(p);
    if (s->as.assign.value == NULL || !expect_end_of_line(p, NULL)) {
        stmt_free(s);
        return NULL;
    }
    return s;
}

/* expr_stmt = expression NEWLINE ; */
static Stmt *parse_expr_stmt(Parser *p) {
    Expr *e = parse_expression(p);
    if (e == NULL) return NULL;
    const char *help = NULL;
    if (peek_is(p, TOK_STRING) && e->kind == EXPR_NAME) {
        help = "a call needs parentheses: print(\"...\")";
    } else if (peek_is(p, TOK_EQUAL)) {
        help = "only a name can be assigned to";
    } else if (peek_is(p, TOK_COLON) && e->kind == EXPR_NAME) {
        help = "to declare a variable write `let x: int = 5`";
    }
    if (!expect_end_of_line(p, help)) {
        expr_free(e);
        return NULL;
    }
    Stmt *s = stmt_new(STMT_EXPR, e->line, e->column);
    s->as.expr = e;
    return s;
}

/* statement = let_stmt | assign_stmt | if_stmt | while_stmt | for_stmt
 *           | fn_decl | return_stmt | break_stmt | continue_stmt | expr_stmt ;
 * Two statements need two tokens of lookahead: IDENT followed by an
 * assignment operator is assign_stmt, and `fn` followed by IDENT is a
 * declaration (otherwise it is a lambda expression). */
static Stmt *parse_statement(Parser *p) {
    Token *t = peek(p);
    switch (t->kind) {
    case TOK_LET:
    case TOK_VAR:
        return parse_let(p);
    case TOK_IF:
        return parse_if(p);
    case TOK_WHILE:
        return parse_while(p);
    case TOK_FOR:
        return parse_for(p);
    case TOK_FN: {
        Token *next = peek_at(p, 1);
        if (next->kind == TOK_IDENT) return parse_fn_decl(p);
        if (next->kind == TOK_RESERVED) { reserved_word_error(p, next); return NULL; }
        if (next->kind != TOK_LPAREN) {
            char summary[160];
            snprintf(summary, sizeof summary, "expected a function name after `fn`, found %s",
                     token_kind_describe(next->kind));
            error_at(p, next, summary, "here", "write `fn name(...):`, or `fn(x) => ...` for a lambda");
            return NULL;
        }
        return parse_expr_stmt(p);
    }
    case TOK_RETURN:
        return parse_return(p);
    case TOK_BREAK:
        return parse_jump(p, STMT_BREAK);
    case TOK_CONTINUE:
        return parse_jump(p, STMT_CONTINUE);
    case TOK_ELIF:
        error_at(p, t, "`elif` without a matching `if`", "here",
                 "`elif` must follow an `if` block, before any `else`");
        return NULL;
    case TOK_ELSE:
        error_at(p, t, "`else` without a matching `if`", "here",
                 "`else` must directly follow an `if` or `elif` block");
        return NULL;
    case TOK_INDENT:
        /* An indented REPL line never gets here: the lexer rejects it. */
        error_at(p, t, "unexpected indentation", "this line is indented",
                 "the previous line does not end with `:`");
        return NULL;
    case TOK_IDENT:
        if (is_assign_op(peek_at(p, 1)->kind)) return parse_assign(p);
        return parse_expr_stmt(p);
    default:
        return parse_expr_stmt(p);
    }
}

/* program   = HEADER { statement } EOF ;
 * repl_line = { statement } EOF ; */
Program *parse_program(TokenList *tokens, bool repl_mode, const Source *src, Diag *err) {
    Parser p;
    p.tokens = tokens;
    p.pos = 0;
    p.repl = repl_mode;
    p.depth = 0;
    p.src = src;
    p.err = err;

    Program *prog = program_new();
    if (!repl_mode) {
        /* The lexer guarantees that a file's first token is the header. */
        Token *header = advance(&p);
        snprintf(prog->edition, sizeof prog->edition, "%s", header->text);
    }
    while (!peek_is(&p, TOK_EOF)) {
        Stmt *s = parse_statement(&p);
        if (s == NULL) {
            program_free(prog);
            return NULL;
        }
        stmt_list_add(&prog->body, s);
    }
    return prog;
}
