#include "lexer.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"
#include "version.h"

#define MAX_PAREN_DEPTH 64
#define MAX_INDENT_DEPTH 64
#define INDENT_WIDTH 4

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
    int indent_stack[MAX_INDENT_DEPTH];  /* column widths of open blocks */
    int indent_depth;                    /* indent_stack[0] is always 0 */
    TokenList *out;
    Diag *err;
} Lexer;

/* ---- token list ------------------------------------------------------- */

/* Appends a token. Side effect: any token except NEWLINE, INDENT, DEDENT
 * and EOF marks the current line as non-empty, which is what makes the
 * lexer emit a NEWLINE at the end of it. */
static void emit_token(Lexer *lx, TokenKind kind, int line, int column,
                       const char *text, size_t length) {
    TokenList *list = lx->out;
    if (list->count == list->capacity) {
        list->capacity = list->capacity == 0 ? 64 : list->capacity * 2;
        list->items = xrealloc(list->items, list->capacity * sizeof(Token));
    }
    Token *t = &list->items[list->count++];
    t->kind = kind;
    t->line = line;
    t->column = column;
    t->length = length;
    t->text = NULL;
    if (text != NULL) t->text = xstrndup(text, length);
    if (kind != TOK_NEWLINE && kind != TOK_INDENT && kind != TOK_DEDENT && kind != TOK_EOF) {
        lx->line_has_tokens = true;
    }
}

void token_list_free(TokenList *list) {
    for (size_t i = 0; i < list->count; i++) free(list->items[i].text);
    free(list->items);
    list->items = NULL;
    list->count = list->capacity = 0;
}

const char *token_kind_describe(TokenKind kind) {
    switch (kind) {
    case TOK_HEADER:   return "version header";
    case TOK_NEWLINE:  return "end of line";
    case TOK_INDENT:   return "indentation";
    case TOK_DEDENT:   return "end of block";
    case TOK_EOF:      return "end of file";
    case TOK_IDENT:    return "identifier";
    case TOK_RESERVED: return "reserved word";
    case TOK_INT:      return "number";
    case TOK_FLOAT:    return "number";
    case TOK_STRING:   return "string";
    case TOK_LET:      return "`let`";
    case TOK_VAR:      return "`var`";
    case TOK_IF:       return "`if`";
    case TOK_ELIF:     return "`elif`";
    case TOK_ELSE:     return "`else`";
    case TOK_AND:      return "`and`";
    case TOK_OR:       return "`or`";
    case TOK_NOT:      return "`not`";
    case TOK_TRUE:     return "`true`";
    case TOK_FALSE:    return "`false`";
    case TOK_FN:       return "`fn`";
    case TOK_THROWS:   return "`throws`";
    case TOK_WHILE:    return "`while`";
    case TOK_FOR:      return "`for`";
    case TOK_IN:       return "`in`";
    case TOK_BREAK:    return "`break`";
    case TOK_CONTINUE: return "`continue`";
    case TOK_RETURN:   return "`return`";
    case TOK_LPAREN:   return "`(`";
    case TOK_RPAREN:   return "`)`";
    case TOK_LBRACKET: return "`[`";
    case TOK_RBRACKET: return "`]`";
    case TOK_COMMA:    return "`,`";
    case TOK_COLON:    return "`:`";
    case TOK_DOT:      return "`.`";
    case TOK_PIPE:     return "`|`";
    case TOK_QUESTION: return "`?`";
    case TOK_ARROW:    return "`->`";
    case TOK_DOT_DOT:  return "`..`";
    case TOK_DOT_DOT_EQUAL: return "`..=`";
    case TOK_FAT_ARROW: return "`=>`";
    case TOK_PLUS:     return "`+`";
    case TOK_MINUS:    return "`-`";
    case TOK_STAR:     return "`*`";
    case TOK_SLASH:    return "`/`";
    case TOK_SLASH_SLASH: return "`//`";
    case TOK_STAR_STAR:   return "`**`";
    case TOK_EQUAL:    return "`=`";
    case TOK_PLUS_EQUAL:  return "`+=`";
    case TOK_MINUS_EQUAL: return "`-=`";
    case TOK_STAR_EQUAL:  return "`*=`";
    case TOK_SLASH_EQUAL: return "`/=`";
    case TOK_SLASH_SLASH_EQUAL: return "`//=`";
    case TOK_EQUAL_EQUAL: return "`==`";
    case TOK_BANG_EQUAL:  return "`!=`";
    case TOK_LESS:        return "`<`";
    case TOK_LESS_EQUAL:  return "`<=`";
    case TOK_GREATER:     return "`>`";
    case TOK_GREATER_EQUAL: return "`>=`";
    }
    return "token";
}

const char *token_kind_name(TokenKind kind) {
    switch (kind) {
    case TOK_HEADER:   return "HEADER";
    case TOK_NEWLINE:  return "NEWLINE";
    case TOK_INDENT:   return "INDENT";
    case TOK_DEDENT:   return "DEDENT";
    case TOK_EOF:      return "EOF";
    case TOK_IDENT:    return "IDENT";
    case TOK_RESERVED: return "RESERVED";
    case TOK_INT:      return "INT";
    case TOK_FLOAT:    return "FLOAT";
    case TOK_STRING:   return "STRING";
    case TOK_LET:      return "LET";
    case TOK_VAR:      return "VAR";
    case TOK_IF:       return "IF";
    case TOK_ELIF:     return "ELIF";
    case TOK_ELSE:     return "ELSE";
    case TOK_AND:      return "AND";
    case TOK_OR:       return "OR";
    case TOK_NOT:      return "NOT";
    case TOK_TRUE:     return "TRUE";
    case TOK_FALSE:    return "FALSE";
    case TOK_FN:       return "FN";
    case TOK_THROWS:   return "THROWS";
    case TOK_WHILE:    return "WHILE";
    case TOK_FOR:      return "FOR";
    case TOK_IN:       return "IN";
    case TOK_BREAK:    return "BREAK";
    case TOK_CONTINUE: return "CONTINUE";
    case TOK_RETURN:   return "RETURN";
    case TOK_LPAREN:   return "LPAREN";
    case TOK_RPAREN:   return "RPAREN";
    case TOK_LBRACKET: return "LBRACKET";
    case TOK_RBRACKET: return "RBRACKET";
    case TOK_COMMA:    return "COMMA";
    case TOK_COLON:    return "COLON";
    case TOK_DOT:      return "DOT";
    case TOK_PIPE:     return "PIPE";
    case TOK_QUESTION: return "QUESTION";
    case TOK_ARROW:    return "ARROW";
    case TOK_DOT_DOT:  return "DOT_DOT";
    case TOK_DOT_DOT_EQUAL: return "DOT_DOT_EQUAL";
    case TOK_FAT_ARROW: return "FAT_ARROW";
    case TOK_PLUS:     return "PLUS";
    case TOK_MINUS:    return "MINUS";
    case TOK_STAR:     return "STAR";
    case TOK_SLASH:    return "SLASH";
    case TOK_SLASH_SLASH: return "SLASH_SLASH";
    case TOK_STAR_STAR:   return "STAR_STAR";
    case TOK_EQUAL:    return "EQUAL";
    case TOK_PLUS_EQUAL:  return "PLUS_EQUAL";
    case TOK_MINUS_EQUAL: return "MINUS_EQUAL";
    case TOK_STAR_EQUAL:  return "STAR_EQUAL";
    case TOK_SLASH_EQUAL: return "SLASH_EQUAL";
    case TOK_SLASH_SLASH_EQUAL: return "SLASH_SLASH_EQUAL";
    case TOK_EQUAL_EQUAL: return "EQUAL_EQUAL";
    case TOK_BANG_EQUAL:  return "BANG_EQUAL";
    case TOK_LESS:        return "LESS";
    case TOK_LESS_EQUAL:  return "LESS_EQUAL";
    case TOK_GREATER:     return "GREATER";
    case TOK_GREATER_EQUAL: return "GREATER_EQUAL";
    }
    return "?";
}

/* ---- keywords --------------------------------------------------------- */

typedef struct { const char *word; TokenKind kind; } Keyword;

/* Words with a meaning today. */
static const Keyword KEYWORDS[] = {
    { "let", TOK_LET }, { "var", TOK_VAR },
    { "if", TOK_IF }, { "elif", TOK_ELIF }, { "else", TOK_ELSE },
    { "and", TOK_AND }, { "or", TOK_OR }, { "not", TOK_NOT },
    { "true", TOK_TRUE }, { "false", TOK_FALSE },
    { "fn", TOK_FN }, { "throws", TOK_THROWS },
    { "while", TOK_WHILE }, { "for", TOK_FOR }, { "in", TOK_IN },
    { "break", TOK_BREAK }, { "continue", TOK_CONTINUE }, { "return", TOK_RETURN },
};

/* Words reserved for later phases (SPEC.md section 2.3). Using one as a
 * name is an error from day one. The contextual modifiers `data open sealed
 * weak override pub allow` are not here: they stay ordinary identifiers. */
static const char *const RESERVED[] = {
    "as", "catch", "class", "defer", "enum", "finally",
    "import", "interface", "is", "macro", "null", "rethrows",
    "select", "self", "spawn", "super", "this", "throw", "try",
    "unsafe", "use", "when",
};

static TokenKind classify_word(const char *text, size_t len) {
    for (size_t i = 0; i < sizeof KEYWORDS / sizeof KEYWORDS[0]; i++) {
        if (strlen(KEYWORDS[i].word) == len && memcmp(KEYWORDS[i].word, text, len) == 0) {
            return KEYWORDS[i].kind;
        }
    }
    for (size_t i = 0; i < sizeof RESERVED / sizeof RESERVED[0]; i++) {
        if (strlen(RESERVED[i]) == len && memcmp(RESERVED[i], text, len) == 0) {
            return TOK_RESERVED;
        }
    }
    return TOK_IDENT;
}

/* ---- cursor ----------------------------------------------------------- */

static bool at_end(const Lexer *lx) {
    return lx->pos >= lx->src->length;
}

static char peek(const Lexer *lx) {
    return at_end(lx) ? '\0' : lx->src->text[lx->pos];
}

static char peek_at(const Lexer *lx, size_t ahead) {
    return lx->pos + ahead >= lx->src->length ? '\0' : lx->src->text[lx->pos + ahead];
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

/* Emits a token of `n` bytes starting at the cursor and moves past it. */
static void emit_operator(Lexer *lx, TokenKind kind, size_t n) {
    emit_token(lx, kind, lx->line, lx->column, NULL, 0);
    while (n-- > 0) advance(lx);
}

/* ---- growable byte buffer for string literals -------------------------- */

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} Buffer;

static void buffer_push(Buffer *b, char c) {
    if (b->length + 1 >= b->capacity) {
        b->capacity = b->capacity == 0 ? 32 : b->capacity * 2;
        b->data = xrealloc(b->data, b->capacity);
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

/* ---- line starts: indentation ----------------------------------------- */

/* Called at the start of every physical line, with the cursor at column 1.
 * Blank lines and comment-only lines produce nothing. Inside brackets
 * leading whitespace is insignificant. Otherwise the indentation is
 * compared with the stack of open blocks (SPEC.md section 3):
 *   deeper by exactly 4           -> INDENT (the parser decides whether a
 *                                    block was expected there)
 *   back to an enclosing level    -> one DEDENT per level
 *   anything else                 -> an error */
typedef struct {
    size_t end;        /* first byte after the leading whitespace */
    int spaces;
    int tab_column;    /* column of the first tab, 0 if none */
    bool blank;        /* nothing but whitespace and maybe a comment */
} Indent;

static Indent measure_indent(const Lexer *lx) {
    const char *text = lx->src->text;
    size_t len = lx->src->length;
    Indent m = { lx->pos, 0, 0, false };
    while (m.end < len && (text[m.end] == ' ' || text[m.end] == '\t')) {
        if (text[m.end] == '\t' && m.tab_column == 0) m.tab_column = (int)(m.end - lx->pos) + 1;
        if (text[m.end] == ' ') m.spaces++;
        m.end++;
    }
    m.blank = m.end >= len || text[m.end] == '\n' || text[m.end] == '\r' || text[m.end] == '#';
    return m;
}

static bool lex_line_start(Lexer *lx) {
    const char *text = lx->src->text;
    size_t len = lx->src->length;
    Indent m = measure_indent(lx);
    size_t p = m.end;
    int spaces = m.spaces;
    int tab_column = m.tab_column;

    if (m.blank) {
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

    int top = lx->indent_stack[lx->indent_depth];
    if (lx->repl && lx->line == 1 && spaces > 0) {
        diag_set(lx->err, lx->line, 1, "unexpected indentation", "this line is indented",
                 "the first line of a REPL input starts at column 1");
        return false;
    }
    if (spaces > top) {
        if (spaces != top + INDENT_WIDTH) {
            char label[80], help[80];
            snprintf(label, sizeof label, "indented by %d spaces", spaces);
            snprintf(help, sizeof help, "indent by exactly 4 spaces: %d here", top + INDENT_WIDTH);
            diag_set(lx->err, lx->line, 1, "inconsistent indentation", label, help);
            return false;
        }
        if (lx->indent_depth + 1 == MAX_INDENT_DEPTH) {
            diag_set(lx->err, lx->line, 1, "blocks nested too deeply", "here", "at most 63 levels");
            return false;
        }
        lx->indent_stack[++lx->indent_depth] = spaces;
        emit_token(lx, TOK_INDENT, lx->line, 1, NULL, 0);
    } else if (spaces < top) {
        while (lx->indent_depth > 0 && lx->indent_stack[lx->indent_depth] > spaces) {
            lx->indent_depth--;
            emit_token(lx, TOK_DEDENT, lx->line, 1, NULL, 0);
        }
        if (lx->indent_stack[lx->indent_depth] != spaces) {
            char label[80], help[96];
            snprintf(label, sizeof label, "%d spaces match no open block", spaces);
            snprintf(help, sizeof help, "expected %d or %d spaces",
                     lx->indent_stack[lx->indent_depth],
                     lx->indent_stack[lx->indent_depth] + INDENT_WIDTH);
            diag_set(lx->err, lx->line, 1, "inconsistent indentation", label, help);
            return false;
        }
    }
    while (lx->pos < p) advance(lx);

    /* `arete 0.1` on any line but the first is a common slip: name it. */
    size_t line_end = p;
    while (line_end < len && text[line_end] != '\n') line_end++;
    if (spaces == 0 && version_line_looks_like_header(text + p, line_end - p)) {
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

/* Consumes digits of the given kind with `_` between them. */
static bool lex_digits(Lexer *lx, int (*is_digit)(int), bool *saw_bad_underscore) {
    bool any = false;
    bool last_was_underscore = false;
    while (is_digit((unsigned char)peek(lx)) || peek(lx) == '_') {
        if (peek(lx) == '_') {
            if (!any || last_was_underscore) *saw_bad_underscore = true;
            last_was_underscore = true;
        } else {
            any = true;
            last_was_underscore = false;
        }
        advance(lx);
    }
    if (last_was_underscore) *saw_bad_underscore = true;
    return any;
}

static int is_bin_digit(int c) { return c == '0' || c == '1'; }
static int is_dec_digit(int c) { return isdigit(c); }
static int is_hex_digit(int c) { return isxdigit(c); }

/* Numbers (SPEC.md 2.3): 42, 1_000_000, 0xFF, 0b101, 1.5, 1e9, 2.5e-3.
 * `1.` and `.5` are not floats, so `1..10` stays INT DOT DOT INT. */
static bool lex_number(Lexer *lx) {
    int line = lx->line, column = lx->column;
    size_t start = lx->pos;
    bool bad_underscore = false;
    bool is_float = false;

    if (peek(lx) == '0' && (peek_at(lx, 1) == 'x' || peek_at(lx, 1) == 'X')) {
        advance(lx); advance(lx);
        if (!lex_digits(lx, is_hex_digit, &bad_underscore)) {
            diag_set(lx->err, line, column, "invalid number literal",
                     "hex digits expected after 0x", "for example 0xFF");
            return false;
        }
    } else if (peek(lx) == '0' && (peek_at(lx, 1) == 'b' || peek_at(lx, 1) == 'B')) {
        advance(lx); advance(lx);
        if (!lex_digits(lx, is_bin_digit, &bad_underscore)) {
            diag_set(lx->err, line, column, "invalid number literal",
                     "binary digits expected after 0b", "for example 0b101");
            return false;
        }
    } else {
        lex_digits(lx, is_dec_digit, &bad_underscore);
        if (peek(lx) == '.' && isdigit((unsigned char)peek_at(lx, 1))) {
            is_float = true;
            advance(lx);
            lex_digits(lx, is_dec_digit, &bad_underscore);
        }
        if (peek(lx) == 'e' || peek(lx) == 'E') {
            size_t save_pos = lx->pos;
            int save_col = lx->column;
            advance(lx);
            if (peek(lx) == '+' || peek(lx) == '-') advance(lx);
            if (isdigit((unsigned char)peek(lx))) {
                is_float = true;
                lex_digits(lx, is_dec_digit, &bad_underscore);
            } else {
                lx->pos = save_pos;             /* not an exponent after all */
                lx->column = save_col;
            }
        }
    }
    if (bad_underscore) {
        diag_set(lx->err, line, column, "invalid number literal",
                 "underscores must sit between digits", "write 1_000_000");
        return false;
    }
    if (isalnum((unsigned char)peek(lx)) || peek(lx) == '_') {
        diag_set(lx->err, lx->line, lx->column, "invalid number literal",
                 "a number cannot run into letters", "put a space or an operator here");
        return false;
    }
    emit_token(lx, is_float ? TOK_FLOAT : TOK_INT, line, column,
               lx->src->text + start, lx->pos - start);
    return true;
}

static void lex_word(Lexer *lx) {
    int line = lx->line, column = lx->column;
    size_t start = lx->pos;
    while (isalnum((unsigned char)peek(lx)) || peek(lx) == '_') advance(lx);
    const char *text = lx->src->text + start;
    size_t len = lx->pos - start;
    TokenKind kind = classify_word(text, len);
    emit_token(lx, kind, line, column,
               (kind == TOK_IDENT || kind == TOK_RESERVED) ? text : NULL,
               (kind == TOK_IDENT || kind == TOK_RESERVED) ? len : 0);
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
    emit_operator(lx, TOK_LPAREN, 1);
    return true;
}

static bool lex_close_paren(Lexer *lx) {
    if (lx->paren_depth == 0) {
        diag_set(lx->err, lx->line, lx->column, "unmatched `)`",
                 "no `(` to close here", "remove it, or add the missing `(`");
        return false;
    }
    lx->paren_depth--;
    emit_operator(lx, TOK_RPAREN, 1);
    return true;
}

/* Operators and punctuation, longest match first. */
static bool lex_operator(Lexer *lx) {
    char c = peek(lx);
    char n = peek_at(lx, 1);
    char n2 = peek_at(lx, 2);
    switch (c) {
    case '+': emit_operator(lx, n == '=' ? TOK_PLUS_EQUAL : TOK_PLUS, n == '=' ? 2 : 1); return true;
    case '-':
        if (n == '>') { emit_operator(lx, TOK_ARROW, 2); return true; }
        emit_operator(lx, n == '=' ? TOK_MINUS_EQUAL : TOK_MINUS, n == '=' ? 2 : 1);
        return true;
    case '*':
        if (n == '*') { emit_operator(lx, TOK_STAR_STAR, 2); return true; }
        emit_operator(lx, n == '=' ? TOK_STAR_EQUAL : TOK_STAR, n == '=' ? 2 : 1);
        return true;
    case '/':
        if (n == '/') {
            if (n2 == '=') { emit_operator(lx, TOK_SLASH_SLASH_EQUAL, 3); return true; }
            emit_operator(lx, TOK_SLASH_SLASH, 2);
            return true;
        }
        emit_operator(lx, n == '=' ? TOK_SLASH_EQUAL : TOK_SLASH, n == '=' ? 2 : 1);
        return true;
    case '=':
        if (n == '>') { emit_operator(lx, TOK_FAT_ARROW, 2); return true; }
        emit_operator(lx, n == '=' ? TOK_EQUAL_EQUAL : TOK_EQUAL, n == '=' ? 2 : 1);
        return true;
    case '<': emit_operator(lx, n == '=' ? TOK_LESS_EQUAL : TOK_LESS, n == '=' ? 2 : 1); return true;
    case '>': emit_operator(lx, n == '=' ? TOK_GREATER_EQUAL : TOK_GREATER, n == '=' ? 2 : 1); return true;
    case '!':
        if (n == '=') { emit_operator(lx, TOK_BANG_EQUAL, 2); return true; }
        diag_set(lx->err, lx->line, lx->column, "unexpected character `!`",
                 "here", "`not` is a word in Arete: write `not x`; `!=` means not equal");
        return false;
    case ',': emit_operator(lx, TOK_COMMA, 1); return true;
    case ':': emit_operator(lx, TOK_COLON, 1); return true;
    case '.':
        if (n == '.') {
            if (n2 == '=') { emit_operator(lx, TOK_DOT_DOT_EQUAL, 3); return true; }
            emit_operator(lx, TOK_DOT_DOT, 2);
            return true;
        }
        emit_operator(lx, TOK_DOT, 1);
        return true;
    case '|': emit_operator(lx, TOK_PIPE, 1); return true;
    case '?': emit_operator(lx, TOK_QUESTION, 1); return true;
    case '[': emit_operator(lx, TOK_LBRACKET, 1); return true;
    case ']': emit_operator(lx, TOK_RBRACKET, 1); return true;
    default:  return false;
    }
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
        if (c == '&') help = "`and` is a word in Arete";
        if (c == '%') help = "`%` is not implemented yet";
        if (c == ';') help = "Arete has no `;`: one statement per line";
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

/* End of a logical line: emit NEWLINE if the line had tokens. */
static void end_logical_line(Lexer *lx) {
    if (lx->line_has_tokens) emit_token(lx, TOK_NEWLINE, lx->line, lx->column, NULL, 0);
    lx->line_has_tokens = false;
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
            if (lx.paren_depth == 0) end_logical_line(&lx);
            advance(&lx);
            lx.at_line_start = true;
        } else if (c == '(') {
            if (!lex_open_paren(&lx)) return false;
        } else if (c == ')') {
            if (!lex_close_paren(&lx)) return false;
        } else if (c == '"' || c == '\'') {
            if (!lex_string(&lx)) return false;
        } else if (isdigit((unsigned char)c)) {
            if (!lex_number(&lx)) return false;
        } else if (isalpha((unsigned char)c) || c == '_') {
            lex_word(&lx);
        } else if (lex_operator(&lx)) {
            /* done */
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
    end_logical_line(&lx);
    while (lx.indent_depth > 0) {
        lx.indent_depth--;
        emit_token(&lx, TOK_DEDENT, lx.line, lx.column, NULL, 0);
    }
    emit_token(&lx, TOK_EOF, lx.line, lx.column, NULL, 0);
    return true;
}

bool lexer_input_incomplete(const Source *src, bool *in_block) {
    TokenList tokens = {0};
    Diag err = {0};
    bool ok = lexer_tokenize(src, false, &tokens, &err);
    bool incomplete = false;
    *in_block = false;
    if (!ok) {
        /* Only an unclosed bracket means "keep typing"; any other error is
         * reported when the input runs. */
        incomplete = strcmp(err.summary, "unclosed `(`") == 0;
    } else {
        TokenKind before_newline = TOK_EOF;
        TokenKind last = TOK_EOF;
        for (size_t i = 0; i < tokens.count; i++) {
            TokenKind k = tokens.items[i].kind;
            if (k == TOK_NEWLINE && last == TOK_COLON) *in_block = true;
            if (k == TOK_NEWLINE) before_newline = last;
            if (k != TOK_NEWLINE && k != TOK_INDENT && k != TOK_DEDENT && k != TOK_EOF) last = k;
        }
        incomplete = (before_newline == TOK_COLON);
    }
    token_list_free(&tokens);
    return incomplete;
}
