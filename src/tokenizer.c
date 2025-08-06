#include "tokenizer.h"
#include <ctype.h>

static const char *keywords[] = {
    "int", "str", "let", "bool", "char", "struct",
    "set", "fn", "ret", "if", "else", "while",
    NULL
};

static const char *operators[] = {
    "+", "-", "*", "/", "%", "**", "!", "~", ".",
    "<", ">", "|", "&", "=",
    "<=", ">=", "||", "&&", "==",
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
    token.value = value ? strdup(value) : NULL;
    token.line = line;
    token.column = column;
    return token;
}

void free_token(Token *token) {
    if (token->value) {
        free(token->value);
        token->value = NULL;
    }
}

void token_array_add(TokenArray *array, Token token) {
    if (array->count >= array->capacity) {
        array->capacity = array->capacity == 0 ? 16 : array->capacity * 2;
        array->tokens = realloc(array->tokens, array->capacity * sizeof(Token));
        if (!array->tokens) {
            fprintf(stderr, "Error: Failed to allocate memory for tokens\n");
            exit(1);
        }
    }
    array->tokens[array->count++] = token;
}

TokenArray *tokenize(const char *source) {
    TokenArray *tokens = malloc(sizeof(TokenArray));
    if (!tokens) {
        fprintf(stderr, "Error: Failed to allocate memory for token array\n");
        exit(1);
    }
    tokens->tokens = NULL;
    tokens->count = 0;
    tokens->capacity = 0;

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
            char *comment = malloc(comment_len + 1);
            strncpy(comment, source + start_idx, comment_len);
            comment[comment_len] = '\0';
            
            token_array_add(tokens, create_token(TOKEN_COMMENT, comment, line, column));
            free(comment);
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
            char *str_value = malloc(str_len + 1);
            strncpy(str_value, source + start_idx, str_len);
            str_value[str_len] = '\0';
            
            token_array_add(tokens, create_token(TOKEN_STRING, str_value, line, column - str_len));
            free(str_value);
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

        // numbers
        if (is_digit(c)) {
            int start_idx = idx;
            while (idx < len && is_digit(source[idx])) {
                idx++;
                column++;
            }
            
            int num_len = idx - start_idx;
            char *num_str = malloc(num_len + 1);
            strncpy(num_str, source + start_idx, num_len);
            num_str[num_len] = '\0';
            
            token_array_add(tokens, create_token(TOKEN_INT, num_str, line, column - num_len));
            free(num_str);
            continue;
        }

        // operators (maximal munch)
        bool found_operator = false;
        for (int op_len = 2; op_len >= 1 && !found_operator; op_len--) {
            if (idx + op_len <= len) {
                char *potential_op = malloc(op_len + 1);
                strncpy(potential_op, source + idx, op_len);
                potential_op[op_len] = '\0';
                
                if (is_operator(potential_op)) {
                    token_array_add(tokens, create_token(TOKEN_OPERATOR, potential_op, line, column));
                    column += op_len;
                    idx += op_len;
                    found_operator = true;
                }
                free(potential_op);
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
            char *identifier = malloc(id_len + 1);
            strncpy(identifier, source + start_idx, id_len);
            identifier[id_len] = '\0';
            
            TokenType type = is_keyword(identifier) ? TOKEN_KEYWORD : TOKEN_IDENTIFIER;
            token_array_add(tokens, create_token(type, identifier, line, column - id_len));
            free(identifier);
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
    if (tokens) {
        for (size_t i = 0; i < tokens->count; i++) {
            free_token(&tokens->tokens[i]);
        }
        free(tokens->tokens);
        free(tokens);
    }
}