#include "lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "version.h"

#define MAX_PAREN_DEPTH 64

typedef struct {
    const Source *src;
    size_t pos;
    int line;
    int column;
    bool at_line_start;
    bool line_has_tokens;
    int paren_depth;
    struct { int line, column; } open_parens[MAX_PAREN_DEPTH];
    TokenList *out;
    Diag *err;
} Lexer;

/* ---- token list ------------------------------------------------------- */

static void *xmalloc(size_t n) {
    void *p = malloc(n);
    if (p == NULL) {
        fputs("fatal: out of memory\n", stderr);
        exit(2);
    }
    return p;
}

static void add_token(Lexer *lx, TokenKind kind, int line, int column,
                      const char *text, size_t length) {
    TokenList *list = lx->out;
    if (list->count == list->capacity) {
        size_t cap = list->capacity == 0 ? 64 : list->capacity * 2;
        Token *grown = realloc(list->items, cap * sizeof(Token));
        if (grown == NULL) {
            fputs("fatal: out of memory\n", stderr);
            exit(2);
        }
        list->items = grown;
        list->capacity = cap;
    }
    Token *t = &list->items[list->count++];
    t->kind = kind;
    t->line = line;
    t->column = column;
    t->length = length;
    if (text != NULL) {
        t->text = xmalloc(length + 1);
        memcpy(t->text, text, length);
        t->text[length] = '\0';
    } else {
        t->text = NULL;
    }
    if (kind != TOK_NEWLINE && kind != TOK_EOF) lx->line_has_tokens = true;
}

void token_list_free(TokenList *list) {
    for (size_t i = 0; i < list->count; i++) free(list->items[i].text);
    free(list->items);
    list->items = NULL;
    list->count = list->capacity = 0;
}

const char *token_kind_describe(TokenKind kind) {
    switch (kind) {
    case TOK_HEADER:  return "version header";
    case TOK_IDENT:   return "identifier";
    case TOK_INT:     return "number";
    case TOK_STRING:  return "string";
    case TOK_LPAREN:  return "`(`";
    case TOK_RPAREN:  return "`)`";
    case TOK_COMMA:   return "`,`";
    case TOK_NEWLINE: return "end of line";
    case TOK_EOF:     return "end of file";
    }
    return "token";
}

/* ---- cursor ----------------------------------------------------------- */

static bool at_end(const Lexer *lx) {
    return lx->pos >= lx->src->length;
}

static char peek(const Lexer *lx) {
    return at_end(lx) ? '\0' : lx->src->text[lx->pos];
}

static char peek_next(const Lexer *lx) {
    return lx->pos + 1 >= lx->src->length ? '\0' : lx->src->text[lx->pos + 1];
}

static void advance(Lexer *lx) {
    if (at_end(lx)) return;
    if (lx->src->text[lx->pos] == '\n') {
        lx->line++;
        lx->column = 1;
    } else {
        lx->column++;
    }
    lx->pos++;
}

static void skip_to_line_end(Lexer *lx) {
    while (!at_end(lx) && peek(lx) != '\n') advance(lx);
}

/* ---- growable byte buffer for string literals -------------------------- */

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} Buffer;

static void buffer_push(Buffer *b, char c) {
    if (b->length + 1 >= b->capacity) {
        size_t cap = b->capacity == 0 ? 32 : b->capacity * 2;
        char *grown = realloc(b->data, cap);
        if (grown == NULL) {
            fputs("fatal: out of memory\n", stderr);
            exit(2);
        }
        b->data = grown;
        b->capacity = cap;
    }
    b->data[b->length++] = c;
}

/* Appends `code` as UTF-8. */
static bool buffer_push_utf8(Buffer *b, unsigned long code) {
    if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
    if (code < 0x80) {
        buffer_push(b, (char)code);
    } else if (code < 0x800) {
        buffer_push(b, (char)(0xC0 | (code >> 6)));
        buffer_push(b, (char)(0x80 | (code & 0x3F)));
    } else if (code < 0x10000) {
        buffer_push(b, (char)(0xE0 | (code >> 12)));
        buffer_push(b, (char)(0x80 | ((code >> 6) & 0x3F)));
        buffer_push(b, (char)(0x80 | (code & 0x3F)));
    } else {
        buffer_push(b, (char)(0xF0 | (code >> 18)));
        buffer_push(b, (char)(0x80 | ((code >> 12) & 0x3F)));
        buffer_push(b, (char)(0x80 | ((code >> 6) & 0x3F)));
        buffer_push(b, (char)(0x80 | (code & 0x3F)));
    }
    return true;
}

/* ---- line starts: indentation ----------------------------------------- */

/* Called at the start of every physical line. Blank lines and comment-only
 * lines produce nothing. Inside brackets leading whitespace is ignored.
 * Otherwise, in phase 3 no line may be indented: there are no blocks yet. */
static bool lex_line_start(Lexer *lx) {
    size_t p = lx->pos;
    size_t len = lx->src->length;
    const char *text = lx->src->text;
    int spaces = 0;
    int tab_column = 0;
    while (p < len && (text[p] == ' ' || text[p] == '\t')) {
        if (text[p] == '\t' && tab_column == 0) tab_column = (int)(p - lx->pos) + 1;
        if (text[p] == ' ') spaces++;
        p++;
    }
    bool blank = p >= len || text[p] == '\n' || text[p] == '\r' || text[p] == '#';
    if (blank) {
        skip_to_line_end(lx);
        if (!at_end(lx)) advance(lx);        /* the newline itself */
        lx->at_line_start = !at_end(lx);
        return true;
    }
    if (lx->paren_depth > 0) {
        while (lx->pos < p) advance(lx);
        return true;
    }
    if (tab_column != 0) {
        diag_set(lx->err, lx->line, tab_column, "tab in indentation",
                 "tab character here", "use 4 spaces");
        return false;
    }
    if (spaces > 0) {
        diag_set(lx->err, lx->line, 1, "unexpected indentation",
                 "this line is indented",
                 "the previous line does not end with `:`");
        return false;
    }
    return true;
}

/* ---- individual tokens ------------------------------------------------ */

static bool lex_string(Lexer *lx) {
    char quote = peek(lx);
    int start_line = lx->line;
    int start_column = lx->column;
    advance(lx);
    Buffer buf = {0};

    for (;;) {
        if (at_end(lx) || peek(lx) == '\n') {
            free(buf.data);
            diag_set(lx->err, start_line, start_column, "unterminated string literal",
                     "string starts here", "add the closing quote");
            return false;
        }
        char c = peek(lx);
        if (c == quote) {
            advance(lx);
            break;
        }
        if (c == '{' || c == '}') {
            free(buf.data);
            diag_set(lx->err, lx->line, lx->column,
                     "string interpolation comes in phase 11",
                     c == '{' ? "`{` is not allowed in a string yet"
                              : "`}` is not allowed in a string yet",
                     "from phase 11 on, `{{` and `}}` write literal braces");
            return false;
        }
        if (c == '\\') {
            int esc_line = lx->line, esc_column = lx->column;
            advance(lx);
            char e = peek(lx);
            switch (e) {
            case 'n':  buffer_push(&buf, '\n'); advance(lx); break;
            case 't':  buffer_push(&buf, '\t'); advance(lx); break;
            case '\\': buffer_push(&buf, '\\'); advance(lx); break;
            case '"':  buffer_push(&buf, '"');  advance(lx); break;
            case '\'': buffer_push(&buf, '\''); advance(lx); break;
            case 'u': {
                advance(lx);
                bool ok = peek(lx) == '{';
                unsigned long code = 0;
                int digits = 0;
                if (ok) {
                    advance(lx);
                    while (isxdigit((unsigned char)peek(lx)) && digits < 7) {
                        char h = peek(lx);
                        code = code * 16 + (unsigned long)(isdigit((unsigned char)h)
                               ? h - '0' : tolower((unsigned char)h) - 'a' + 10);
                        digits++;
                        advance(lx);
                    }
                    ok = digits >= 1 && digits <= 6 && peek(lx) == '}';
                }
                if (!ok || !buffer_push_utf8(&buf, code)) {
                    free(buf.data);
                    diag_set(lx->err, esc_line, esc_column, "invalid unicode escape",
                             "expected `\\u{...}` with 1 to 6 hex digits",
                             "for example \\u{1F600}");
                    return false;
                }
                advance(lx);                 /* the closing brace */
                break;
            }
            default: {
                free(buf.data);
                char label[64];
                if (isprint((unsigned char)e)) {
                    snprintf(label, sizeof label, "`\\%c` is not an escape sequence", e);
                } else {
                    snprintf(label, sizeof label, "unknown escape sequence");
                }
                diag_set(lx->err, esc_line, esc_column, "unknown escape sequence",
                         label, "valid escapes: \\n \\t \\\\ \\\" \\' \\u{...}");
                return false;
            }
            }
            continue;
        }
        buffer_push(&buf, c);
        advance(lx);
    }
    add_token(lx, TOK_STRING, start_line, start_column,
              buf.data == NULL ? "" : buf.data, buf.length);
    free(buf.data);
    return true;
}

static bool lex_number(Lexer *lx) {
    int line = lx->line, column = lx->column;
    size_t start = lx->pos;
    while (isdigit((unsigned char)peek(lx)) || peek(lx) == '_') advance(lx);
    const char *text = lx->src->text + start;
    size_t len = lx->pos - start;
    bool bad = text[len - 1] == '_';
    for (size_t i = 1; i < len && !bad; i++) {
        if (text[i] == '_' && text[i - 1] == '_') bad = true;
    }
    if (bad) {
        diag_set(lx->err, line, column, "invalid number literal",
                 "underscores must sit between digits", "write 1_000_000");
        return false;
    }
    add_token(lx, TOK_INT, line, column, text, len);
    return true;
}

static void lex_identifier(Lexer *lx) {
    int line = lx->line, column = lx->column;
    size_t start = lx->pos;
    while (isalnum((unsigned char)peek(lx)) || peek(lx) == '_') advance(lx);
    add_token(lx, TOK_IDENT, line, column, lx->src->text + start, lx->pos - start);
}

static bool lex_unexpected(Lexer *lx) {
    unsigned char c = (unsigned char)peek(lx);
    char summary[96];
    const char *help = NULL;
    if (c >= 0x80) {
        /* Show the whole UTF-8 character, not just its first byte. */
        size_t n = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 1;
        if (lx->pos + n > lx->src->length) n = lx->src->length - lx->pos;
        snprintf(summary, sizeof summary, "unexpected character `%.*s`",
                 (int)n, lx->src->text + lx->pos);
        help = "identifiers use ASCII letters, digits and `_` for now";
    } else if (isprint(c)) {
        snprintf(summary, sizeof summary, "unexpected character `%c`", c);
    } else {
        snprintf(summary, sizeof summary, "unexpected character (byte 0x%02X)", c);
    }
    diag_set(lx->err, lx->line, lx->column, summary, "here", help);
    return false;
}

/* ---- main loop -------------------------------------------------------- */

bool lexer_tokenize(const Source *src, bool expect_header,
                    TokenList *out, Diag *err) {
    Lexer lx;
    memset(&lx, 0, sizeof lx);
    lx.src = src;
    lx.out = out;
    lx.err = err;
    out->items = NULL;
    out->count = out->capacity = 0;

    /* A UTF-8 byte order mark at the very start is skipped. */
    size_t start = 0;
    if (src->length >= 3 && (unsigned char)src->text[0] == 0xEF
        && (unsigned char)src->text[1] == 0xBB && (unsigned char)src->text[2] == 0xBF) {
        start = 3;
    }

    if (expect_header) {
        size_t next_line = 0;
        if (!version_check_header(src, start, &next_line, err)) return false;
        add_token(&lx, TOK_HEADER, 1, 1, ARETE_LANG_VERSION, strlen(ARETE_LANG_VERSION));
        lx.line_has_tokens = false;
        lx.pos = next_line;
        lx.line = 2;
        lx.column = 1;
    } else {
        lx.pos = start;
        lx.line = 1;
        lx.column = 1;
    }
    lx.at_line_start = true;

    for (;;) {
        if (lx.at_line_start) {
            lx.at_line_start = false;
            if (!lex_line_start(&lx)) return false;
            if (lx.at_line_start) continue;
        }
        if (at_end(&lx)) break;

        char c = peek(&lx);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(&lx);
        } else if (c == '#') {
            skip_to_line_end(&lx);
        } else if (c == '\n') {
            if (lx.paren_depth == 0) {
                if (lx.line_has_tokens) add_token(&lx, TOK_NEWLINE, lx.line, lx.column, NULL, 0);
                lx.line_has_tokens = false;
            }
            advance(&lx);
            lx.at_line_start = true;
        } else if (c == '(') {
            if (lx.paren_depth == MAX_PAREN_DEPTH) {
                diag_set(err, lx.line, lx.column, "too many nested parentheses", "here", NULL);
                return false;
            }
            lx.open_parens[lx.paren_depth].line = lx.line;
            lx.open_parens[lx.paren_depth].column = lx.column;
            lx.paren_depth++;
            add_token(&lx, TOK_LPAREN, lx.line, lx.column, NULL, 0);
            advance(&lx);
        } else if (c == ')') {
            if (lx.paren_depth == 0) {
                diag_set(err, lx.line, lx.column, "unmatched `)`",
                         "no `(` to close here", "remove it, or add the missing `(`");
                return false;
            }
            lx.paren_depth--;
            add_token(&lx, TOK_RPAREN, lx.line, lx.column, NULL, 0);
            advance(&lx);
        } else if (c == ',') {
            add_token(&lx, TOK_COMMA, lx.line, lx.column, NULL, 0);
            advance(&lx);
        } else if (c == '"' || c == '\'') {
            if (!lex_string(&lx)) return false;
        } else if (isdigit((unsigned char)c)) {
            if (!lex_number(&lx)) return false;
        } else if (isalpha((unsigned char)c) || c == '_') {
            lex_identifier(&lx);
        } else {
            return lex_unexpected(&lx);
        }
    }

    if (lx.paren_depth > 0) {
        int i = lx.paren_depth - 1;
        diag_set(err, lx.open_parens[i].line, lx.open_parens[i].column,
                 "unclosed `(`", "opened here", "add the matching `)`");
        return false;
    }
    if (lx.line_has_tokens) add_token(&lx, TOK_NEWLINE, lx.line, lx.column, NULL, 0);
    add_token(&lx, TOK_EOF, lx.line, lx.column, NULL, 0);
    (void)peek_next;
    return true;
}
