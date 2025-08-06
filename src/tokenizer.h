#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "types.h"

TokenArray *tokenize(const char *source);
void free_token_array(TokenArray *tokens);
void token_array_add(TokenArray *array, Token token);

bool is_keyword(const char *str);
bool is_operator(const char *str);
bool is_digit(char c);
bool is_alpha(char c);
bool is_ident(char c);
bool is_whitespace(char c);

Token create_token(TokenType type, const char *value, int line, int column);
void free_token(Token *token);

#endif // TOKENIZER_H