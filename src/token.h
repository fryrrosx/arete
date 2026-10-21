/* token.h - what the lexer produces. */
#ifndef ARETE_TOKEN_H
#define ARETE_TOKEN_H

#include <stddef.h>

typedef enum {
    TOK_HEADER,   /* line 1: arete <major>.<minor>; text is the version it matched */
    TOK_IDENT,    /* text is the name */
    TOK_INT,      /* text is the digits as written (converted in phase 4) */
    TOK_STRING,   /* text is the string with escapes already processed */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_COMMA,
    TOK_NEWLINE,  /* end of a logical line */
    TOK_EOF
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

/* Human-readable name for error messages: "identifier", "`(`", "end of line". */
const char *token_kind_describe(TokenKind kind);

#endif
