/* token.h - what the lexer produces. */
#ifndef ARETE_TOKEN_H
#define ARETE_TOKEN_H

#include <stddef.h>

typedef enum {
    /* structure */
    TOK_HEADER,        /* line 1: arete <major>.<minor>; text is the version it matched */
    TOK_NEWLINE,       /* end of a logical line */
    TOK_INDENT,        /* the next line is one level deeper */
    TOK_DEDENT,        /* one level closed */
    TOK_EOF,

    /* literals and names */
    TOK_IDENT,         /* text is the name */
    TOK_RESERVED,      /* a word reserved for a later phase; text is the word */
    TOK_INT,           /* text is the literal as written */
    TOK_FLOAT,         /* text is the literal as written */
    TOK_STRING,        /* text is the string with escapes already processed */

    /* keywords */
    TOK_LET, TOK_VAR,
    TOK_IF, TOK_ELIF, TOK_ELSE,
    TOK_AND, TOK_OR, TOK_NOT,
    TOK_TRUE, TOK_FALSE,
    TOK_FN, TOK_THROWS,

    /* punctuation and operators */
    TOK_LPAREN, TOK_RPAREN, TOK_LBRACKET, TOK_RBRACKET,
    TOK_COMMA, TOK_COLON, TOK_DOT, TOK_PIPE, TOK_QUESTION, TOK_ARROW,
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_SLASH_SLASH, TOK_STAR_STAR,
    TOK_EQUAL, TOK_PLUS_EQUAL, TOK_MINUS_EQUAL, TOK_STAR_EQUAL,
    TOK_SLASH_EQUAL, TOK_SLASH_SLASH_EQUAL,
    TOK_EQUAL_EQUAL, TOK_BANG_EQUAL, TOK_LESS, TOK_LESS_EQUAL,
    TOK_GREATER, TOK_GREATER_EQUAL
} TokenKind;

typedef struct {
    TokenKind kind;
    int line;        /* 1-based */
    int column;      /* 1-based, in bytes */
    char *text;      /* owned; NULL for tokens that carry no text */
    size_t length;   /* length of text in bytes */
} Token;

typedef struct {
    Token *items;
    size_t count;
    size_t capacity;
} TokenList;

void token_list_free(TokenList *list);

/* For error messages: "identifier", "`(`", "end of line". */
const char *token_kind_describe(TokenKind kind);

/* For --dump-tokens: "IDENT", "LPAREN", "NEWLINE". */
const char *token_kind_name(TokenKind kind);

#endif
