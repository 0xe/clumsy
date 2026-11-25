#include "tokenizer.h"
#include "arena.h"
#include <ctype.h>

static const char *keywords[] = {
    "int", "str", "let", "pub", "bool", "char", "struct",
    "set", "fn", "ret", "if", "else", "while",
    "break", "continue",
    "extern", "include", "null",
    NULL
};

static const char *operators[] = {
    "+", "-", "*", "/", "%", "**", "!", "~", ".",
    "<", ">", "|", "&", "=",
    "<=", ">=", "||", "&&",
    ">>", "<<",
    NULL
};

bool is_keyword(const char *str) {
    for (int i = 0; keywords[i] != NULL; i++) {
        if (strcmp(str, keywords[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool is_operator(const char *str) {
    for (int i = 0; operators[i] != NULL; i++) {
        if (strcmp(str, operators[i]) == 0) {
            return true;
        }
    }
    return false;
}

bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_alpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool is_ident(char c) {
    return is_digit(c) || is_alpha(c) || c == '_';
}

bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\0';
}

Token create_token(TokenType type, const char *value, int line, int column) {
    Token token;
    token.type = type;
    token.value = value ? arena_strdup(&g_arena_compile, value) : NULL;
    token.line = line;
    token.column = column;
    return token;
}

void free_token(Token *token) {
    (void)token;
}

void token_array_add(TokenArray *array, Token token) {
    if (array->count >= array->capacity) {
        size_t old_capacity = array->capacity;
        array->capacity = old_capacity == 0 ? 16 : old_capacity * 2;
        array->tokens = arena_grow(&g_arena_compile, array->tokens,
                                   old_capacity * sizeof(Token),
                                   array->capacity * sizeof(Token));
    }
    array->tokens[array->count++] = token;
}

TokenArray *tokenize(const char *source) {
    TokenArray *tokens = arena_calloc(&g_arena_compile, 1, sizeof(TokenArray));

    int line = 1;
    int column = 1;
    int idx = 0;
    int len = strlen(source);

    while (idx < len) {
        char c = source[idx];

        if (c == '\n') {
            token_array_add(tokens, create_token(TOKEN_NEWLINE, "\n", line, column));
            line++;
            column = 1;
            idx++;
            continue;
        }

        // comments
        if (c == '/' && idx + 1 < len && source[idx + 1] == '/') {
            int start_idx = idx;
            idx += 2; // Skip "//"
            while (idx < len && source[idx] != '\n') {
                idx++;
            }
            int comment_len = idx - start_idx;
            char *comment = arena_alloc(&g_arena_compile, comment_len + 1);
            memcpy(comment, source + start_idx, comment_len);
            comment[comment_len] = '\0';

            token_array_add(tokens, create_token(TOKEN_COMMENT, comment, line, column));
            column += comment_len;
            continue;
        }

        if (c == '(') {
            token_array_add(tokens, create_token(TOKEN_LPAREN, "(", line, column));
            column++;
            idx++;
            continue;
        }

        if (c == ')') {
            token_array_add(tokens, create_token(TOKEN_RPAREN, ")", line, column));
            column++;
            idx++;
            continue;
        }

        if (c == '[') {
            token_array_add(tokens, create_token(TOKEN_LBRACKET, "[", line, column));
            column++;
            idx++;
            continue;
        }
        if (c == ']') {
            token_array_add(tokens, create_token(TOKEN_RBRACKET, "]", line, column));
            column++;
            idx++;
            continue;
        }

        if (c == '\'') {
            token_array_add(tokens, create_token(TOKEN_QUOTE, "quote", line, column));
            column++;
            idx++;
            continue;
        }

        // * and & are always tokenized as TOKEN_OPERATOR via the operator loop below
        // string literals
        if (c == '"') {
            int start_idx = idx;
            idx++; // opening quote
            column++;
            
            while (idx < len && source[idx] != '"') {
                if (source[idx] == '\\' && idx + 1 < len) {
                    idx += 2; // escape sequence
                    column += 2;
                } else {
                    idx++;
                    column++;
                }
            }
            
            if (idx < len) {
                idx++; // closing quote
                column++;
            }
            
            int str_len = idx - start_idx;
            char *str_value = arena_alloc(&g_arena_compile, str_len + 1);
            memcpy(str_value, source + start_idx, str_len);
            str_value[str_len] = '\0';

            token_array_add(tokens, create_token(TOKEN_STRING, str_value, line, column - str_len));
            continue;
        }

        // character literals scheme style - `#\c`
        if (c == '#' && idx + 1 < len && source[idx + 1] == '\\' && idx + 2 < len) {
            char char_value = source[idx + 2];
            char char_str[4] = {'#', '\\', char_value, '\0'};
            token_array_add(tokens, create_token(TOKEN_CHAR, char_str, line, column));
            column += 3;
            idx += 3;
            continue;
        }

        // struct literals and other # constructs
        if (c == '#' && idx + 1 < len && source[idx + 1] == '(') {
            token_array_add(tokens, create_token(TOKEN_OPERATOR, "#", line, column));
            column++;
            idx++;
            continue;
        }

        // # character
        if (c == '#') {
            token_array_add(tokens, create_token(TOKEN_OPERATOR, "#", line, column));
            column++;
            idx++;
            continue;
        }

        // negative numbers: - immediately followed by digit
        if (c == '-' && idx + 1 < len && is_digit(source[idx + 1])) {
            int start_idx = idx;
            idx++; // skip the minus
            if (source[idx] == '0' && idx + 1 < len && (source[idx + 1] == 'x' || source[idx + 1] == 'X')) {
                idx += 2; column += 2;
                while (idx < len && (is_digit(source[idx]) ||
                       (source[idx] >= 'a' && source[idx] <= 'f') ||
                       (source[idx] >= 'A' && source[idx] <= 'F'))) {
                    idx++; column++;
                }
            } else {
                while (idx < len && is_digit(source[idx])) {
                    idx++; column++;
                }
            }

            int num_len = idx - start_idx;
            char *num_str = arena_alloc(&g_arena_compile, num_len + 1);
            memcpy(num_str, source + start_idx, num_len);
            num_str[num_len] = '\0';

            token_array_add(tokens, create_token(TOKEN_INT, num_str, line, column - num_len));
            continue;
        }

        // numbers (decimal and hex 0x...)
        if (is_digit(c)) {
            int start_idx = idx;
            if (c == '0' && idx + 1 < len && (source[idx + 1] == 'x' || source[idx + 1] == 'X')) {
                // hex literal
                idx += 2; column += 2;
                while (idx < len && (is_digit(source[idx]) ||
                       (source[idx] >= 'a' && source[idx] <= 'f') ||
                       (source[idx] >= 'A' && source[idx] <= 'F'))) {
                    idx++; column++;
                }
            } else {
                while (idx < len && is_digit(source[idx])) {
                    idx++; column++;
                }
            }

            int num_len = idx - start_idx;
            char *num_str = arena_alloc(&g_arena_compile, num_len + 1);
            memcpy(num_str, source + start_idx, num_len);
            num_str[num_len] = '\0';

            token_array_add(tokens, create_token(TOKEN_INT, num_str, line, column - num_len));
            continue;
        }

        // operators (maximal munch)
        bool found_operator = false;
        for (int op_len = 2; op_len >= 1 && !found_operator; op_len--) {
            if (idx + op_len <= len) {
                char potential_op[3];
                memcpy(potential_op, source + idx, op_len);
                potential_op[op_len] = '\0';

                if (is_operator(potential_op)) {
                    token_array_add(tokens, create_token(TOKEN_OPERATOR, potential_op, line, column));
                    column += op_len;
                    idx += op_len;
                    found_operator = true;
                }
            }
        }
        if (found_operator) {
            continue;
        }

        // ids
        if (is_alpha(c) || c == '_') {
            int start_idx = idx;
            while (idx < len && is_ident(source[idx])) {
                idx++;
                column++;
            }
            
            int id_len = idx - start_idx;
            char *identifier = arena_alloc(&g_arena_compile, id_len + 1);
            memcpy(identifier, source + start_idx, id_len);
            identifier[id_len] = '\0';

            TokenType type = is_keyword(identifier) ? TOKEN_KEYWORD : TOKEN_IDENTIFIER;
            token_array_add(tokens, create_token(type, identifier, line, column - id_len));
            continue;
        }

        // whitespace
        if (is_whitespace(c)) {
            column++;
            idx++;
            continue;
        }

        // Unknown character
        fprintf(stderr, "Warning: Unknown character '%c' at line %d, column %d\n", c, line, column);
        column++;
        idx++;
    }

    // end of token stream
    token_array_add(tokens, create_token(TOKEN_EOF, NULL, line, column));
    
    return tokens;
}

void free_token_array(TokenArray *tokens) {
    (void)tokens;
}