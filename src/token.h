#ifndef TOKEN_H
#define TOKEN_H

#include <stddef.h>

typedef enum {
    TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_LBRACKET, TOKEN_RBRACKET, TOKEN_QUOTE,
    TOKEN_INT, TOKEN_STRING, TOKEN_CHAR, TOKEN_IDENTIFIER, TOKEN_KEYWORD,
    TOKEN_OPERATOR, TOKEN_COMMENT, TOKEN_NEWLINE, TOKEN_EXTERN, TOKEN_EOF
} TokenType;

typedef struct {
    TokenType type;
    char *value;
    int line;
    int column;
} Token;

typedef struct {
    Token *tokens;
    size_t count;
    size_t capacity;
} TokenArray;

#endif
