#ifndef TOKENIZER_H
#define TOKENIZER_H

#include "types.h"

// Tokenizer functions
TokenArray *tokenize(const char *source);
void free_token_array(TokenArray *tokens);
void token_array_add(TokenArray *array, Token token);

// Helper functions
bool is_keyword(const char *str);
bool is_operator(const char *str);
bool is_digit(char c);
bool is_alpha(char c);
bool is_alnum_or_underscore(char c);
bool is_whitespace(char c);

// Token creation helpers
Token create_token(TokenType type, const char *value, int line, int column);
void free_token(Token *token);

#endif // TOKENIZER_H