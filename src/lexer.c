#include "lexer.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "version.h"

#define MAX_PAREN_DEPTH 64

typedef struct {
    const Source *src;
    bool repl;             /* a REPL line: no header, different hints */
    size_t pos;
    int line;
    int column;
    bool at_line_start;
    bool line_has_tokens;  /* decides whether a NEWLINE token is emitted */
    int paren_depth;
    struct { int line, column; } open_parens[MAX_PAREN_DEPTH];
    TokenList *out;
    Diag *err;
} Lexer;

static void out_of_memory(void) {
    fputs("fatal: out of memory\n", stderr);
    exit(2);
}

/* ---- token list ------------------------------------------------------- */

/* Appends a token. Side effect: any token except NEWLINE and EOF marks the
 * current line as non-empty, which is what makes the lexer emit a NEWLINE
 * at the end of it. */
static void emit_token(Lexer *lx, TokenKind kind, int line, int column,
                       const char *text, size_t length) {
    TokenList *list = lx->out;
    if (list->count == list->capacity) {
        size_t cap = list->capacity == 0 ? 64 : list->capacity * 2;
        Token *grown = realloc(list->items, cap * sizeof(Token));
        if (grown == NULL) out_of_memory();
        list->items = grown;
        list->capacity = cap;
    }
    Token *t = &list->items[list->count++];
    t->kind = kind;
    t->line = line;
    t->column = column;
    t->length = length;
    t->text = NULL;
    if (text != NULL) {
        t->text = malloc(length + 1);
        if (t->text == NULL) out_of_memory();
        memcpy(t->text, text, length);
        t->text[length] = '\0';
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

/* Moves one byte forward, keeping line and column up to date. Positions
 * saturate at INT_MAX instead of overflowing on absurdly long input. */
static void advance(Lexer *lx) {
    if (at_end(lx)) return;
    if (lx->src->text[lx->pos] == '\n') {
        if (lx->line < INT_MAX) lx->line++;
        lx->column = 1;
    } else if (lx->column < INT_MAX) {
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
        if (grown == NULL) out_of_memory();
        b->data = grown;
        b->capacity = cap;
    }
    b->data[b->length++] = c;
}

/* Appends `code` as UTF-8. False for values that are not valid code points. */
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

/* ---- line starts ------------------------------------------------------ */

/* Called at the start of every physical line, with the cursor at column 1.
 * Blank lines and comment-only lines produce nothing. Inside brackets
 * leading whitespace is insignificant. Otherwise, in phase 3 no line may be
 * indented, because there are no blocks yet. */
static bool lex_line_start(Lexer *lx) {
    const char *text = lx->src->text;
    size_t len = lx->src->length;
    size_t p = lx->pos;
    int spaces = 0;
    int tab_column = 0;                         /* 0 means no tab seen */
    while (p < len && (text[p] == ' ' || text[p] == '\t')) {
        if (text[p] == '\t' && tab_column == 0) tab_column = (int)(p - lx->pos) + 1;
        if (text[p] == ' ') spaces++;
        p++;
    }

    bool blank = p >= len || text[p] == '\n' || text[p] == '\r' || text[p] == '#';
    if (blank) {
        skip_to_line_end(lx);
        if (!at_end(lx)) advance(lx);           /* the newline itself */
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
                 lx->repl ? "lines in the REPL start at column 1"
                          : "the previous line does not end with `:`");
        return false;
    }

    /* `arete 0.1` on any line but the first is a common slip: name it. */
    size_t line_end = p;
    while (line_end < len && text[line_end] != '\n') line_end++;
    if (version_line_looks_like_header(text + p, line_end - p)) {
        diag_set(lx->err, lx->line, 1, "version header belongs on line 1 only",
                 lx->repl ? "no header is needed in the REPL" : "a second header here",
                 lx->repl ? "just type the program, for example print(\"hi\")"
                          : "remove this line; line 1 already has the header");
        return false;
    }
    return true;
}

/* ---- individual tokens ------------------------------------------------ */

/* After `\u`: expects `{` 1 to 6 hex digits `}` and appends the character. */
static bool lex_unicode_escape(Lexer *lx, Buffer *buf, int esc_line, int esc_column) {
    unsigned long code = 0;
    int digits = 0;
    bool ok = peek(lx) == '{';
    if (ok) {
        advance(lx);
        while (isxdigit((unsigned char)peek(lx)) && digits < 7) {
            char h = peek(lx);
            int value = isdigit((unsigned char)h) ? h - '0'
                                                  : tolower((unsigned char)h) - 'a' + 10;
            code = code * 16 + (unsigned long)value;
            digits++;
            advance(lx);
        }
        ok = digits >= 1 && digits <= 6 && peek(lx) == '}';
    }
    if (!ok || !buffer_push_utf8(buf, code)) {
        diag_set(lx->err, esc_line, esc_column, "invalid unicode escape",
                 "expected `\\u{...}` with 1 to 6 hex digits", "for example \\u{1F600}");
        return false;
    }
    advance(lx);                                /* the closing brace */
    return true;
}

/* After a backslash inside a string. */
static bool lex_escape(Lexer *lx, Buffer *buf) {
    int esc_line = lx->line, esc_column = lx->column;
    advance(lx);                                /* the backslash */
    char e = peek(lx);
    switch (e) {
    case 'n':  buffer_push(buf, '\n'); break;
    case 't':  buffer_push(buf, '\t'); break;
    case '\\': buffer_push(buf, '\\'); break;
    case '"':  buffer_push(buf, '"');  break;
    case '\'': buffer_push(buf, '\''); break;
    case 'u':
        advance(lx);
        return lex_unicode_escape(lx, buf, esc_line, esc_column);
    default: {
        char label[64];
        snprintf(label, sizeof label, "`\\%c` is not an escape sequence",
                 isprint((unsigned char)e) ? e : '?');
        diag_set(lx->err, esc_line, esc_column, "unknown escape sequence", label,
                 "valid escapes: \\n \\t \\\\ \\\" \\' \\u{...}");
        return false;
    }
    }
    advance(lx);                                /* the escape letter */
    return true;
}

static bool lex_string(Lexer *lx) {
    char quote = peek(lx);
    int start_line = lx->line;
    int start_column = lx->column;
    advance(lx);
    Buffer buf = {0};
    bool ok = true;

    for (;;) {
        if (at_end(lx) || peek(lx) == '\n') {
            diag_set(lx->err, start_line, start_column, "unterminated string literal",
                     "string starts here", "add the closing quote");
            ok = false;
            break;
        }
        char c = peek(lx);
        if (c == quote) {
            advance(lx);
            break;
        }
        if (c == '{' || c == '}') {
            diag_set(lx->err, lx->line, lx->column,
                     "string interpolation comes in phase 11",
                     c == '{' ? "`{` is not allowed in a string yet"
                              : "`}` is not allowed in a string yet",
                     "from phase 11 on, `{{` and `}}` write literal braces");
            ok = false;
            break;
        }
        if (c == '\\') {
            /* A backslash right before the end of the line or file means the
             * string was never closed; let the loop's first check say so. */
            size_t next = lx->pos + 1;
            if (next >= lx->src->length || lx->src->text[next] == '\n') {
                advance(lx);
                continue;
            }
            if (!lex_escape(lx, &buf)) {
                ok = false;
                break;
            }
            continue;
        }
        buffer_push(&buf, c);
        advance(lx);
    }
    if (ok) {
        emit_token(lx, TOK_STRING, start_line, start_column,
                   buf.data == NULL ? "" : buf.data, buf.length);
    }
    free(buf.data);
    return ok;
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
    emit_token(lx, TOK_INT, line, column, text, len);
    return true;
}

static void lex_identifier(Lexer *lx) {
    int line = lx->line, column = lx->column;
    size_t start = lx->pos;
    while (isalnum((unsigned char)peek(lx)) || peek(lx) == '_') advance(lx);
    emit_token(lx, TOK_IDENT, line, column, lx->src->text + start, lx->pos - start);
}

static bool lex_open_paren(Lexer *lx) {
    if (lx->paren_depth == MAX_PAREN_DEPTH) {
        diag_set(lx->err, lx->line, lx->column, "too many nested parentheses",
                 "here", "at most 64 levels");
        return false;
    }
    lx->open_parens[lx->paren_depth].line = lx->line;
    lx->open_parens[lx->paren_depth].column = lx->column;
    lx->paren_depth++;
    emit_token(lx, TOK_LPAREN, lx->line, lx->column, NULL, 0);
    advance(lx);
    return true;
}

static bool lex_close_paren(Lexer *lx) {
    if (lx->paren_depth == 0) {
        diag_set(lx->err, lx->line, lx->column, "unmatched `)`",
                 "no `(` to close here", "remove it, or add the missing `(`");
        return false;
    }
    lx->paren_depth--;
    emit_token(lx, TOK_RPAREN, lx->line, lx->column, NULL, 0);
    advance(lx);
    return true;
}

/* Reports a byte that starts no token. A well-formed UTF-8 character is
 * shown whole; anything else is shown as a byte value. */
static bool lex_unexpected(Lexer *lx) {
    const char *text = lx->src->text;
    unsigned char c = (unsigned char)text[lx->pos];
    char summary[96];
    const char *help = NULL;

    size_t want = (c >= 0xF0 && c < 0xF8) ? 4 : (c >= 0xE0) ? 3 : (c >= 0xC0) ? 2 : 0;
    size_t have = 1;
    while (want > 0 && have < want && lx->pos + have < lx->src->length
           && ((unsigned char)text[lx->pos + have] & 0xC0) == 0x80) {
        have++;
    }
    if (want > 0 && have == want) {
        snprintf(summary, sizeof summary, "unexpected character `%.*s`",
                 (int)have, text + lx->pos);
        help = "identifiers use ASCII letters, digits and `_` for now";
    } else if (c < 0x80 && isprint(c)) {
        snprintf(summary, sizeof summary, "unexpected character `%c`", c);
    } else {
        snprintf(summary, sizeof summary, "unexpected character (byte 0x%02X)", c);
    }
    diag_set(lx->err, lx->line, lx->column, summary, "here", help);
    return false;
}

/* ---- setup and main loop ----------------------------------------------- */

/* Skips a byte order mark, checks the version header when one is expected,
 * and positions the cursor on the first line of program text. */
static bool lexer_init(Lexer *lx, const Source *src, bool expect_header,
                       TokenList *out, Diag *err) {
    memset(lx, 0, sizeof *lx);
    lx->src = src;
    lx->repl = !expect_header;
    lx->out = out;
    lx->err = err;
    out->items = NULL;
    out->count = out->capacity = 0;

    size_t start = 0;
    if (src->length >= 3 && (unsigned char)src->text[0] == 0xEF
        && (unsigned char)src->text[1] == 0xBB && (unsigned char)src->text[2] == 0xBF) {
        start = 3;
    }
    lx->pos = start;
    lx->line = 1;
    lx->column = 1;
    lx->at_line_start = true;

    if (expect_header) {
        size_t next_line = 0;
        if (!version_check_header(src, start, &next_line, err)) return false;
        emit_token(lx, TOK_HEADER, 1, 1, ARETE_LANG_VERSION, strlen(ARETE_LANG_VERSION));
        lx->line_has_tokens = false;
        lx->pos = next_line;
        lx->line = 2;
    }
    return true;
}

bool lexer_tokenize(const Source *src, bool expect_header,
                    TokenList *out, Diag *err) {
    Lexer lx;
    if (!lexer_init(&lx, src, expect_header, out, err)) return false;

    for (;;) {
        if (lx.at_line_start) {
            lx.at_line_start = false;
            if (!lex_line_start(&lx)) return false;
            if (lx.at_line_start) continue;    /* that line was blank */
        }
        if (at_end(&lx)) break;

        char c = peek(&lx);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(&lx);
        } else if (c == '#') {
            skip_to_line_end(&lx);
        } else if (c == '\n') {
            if (lx.paren_depth == 0) {
                if (lx.line_has_tokens) emit_token(&lx, TOK_NEWLINE, lx.line, lx.column, NULL, 0);
                lx.line_has_tokens = false;
            }
            advance(&lx);
            lx.at_line_start = true;
        } else if (c == '(') {
            if (!lex_open_paren(&lx)) return false;
        } else if (c == ')') {
            if (!lex_close_paren(&lx)) return false;
        } else if (c == ',') {
            emit_token(&lx, TOK_COMMA, lx.line, lx.column, NULL, 0);
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
    if (lx.line_has_tokens) emit_token(&lx, TOK_NEWLINE, lx.line, lx.column, NULL, 0);
    emit_token(&lx, TOK_EOF, lx.line, lx.column, NULL, 0);
    return true;
}
