#ifndef TYPES_H
#define TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// Token types
typedef enum {
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_QUOTE,
    TOKEN_INT,
    TOKEN_STRING,
    TOKEN_CHAR,
    TOKEN_IDENTIFIER,
    TOKEN_KEYWORD,
    TOKEN_OPERATOR,
    TOKEN_COMMENT,
    TOKEN_NEWLINE,
    TOKEN_EOF
} TokenType;

// Token structure
typedef struct {
    TokenType type;
    char *value;
    int line;
    int column;
} Token;

// Dynamic array for tokens
typedef struct {
    Token *tokens;
    size_t count;
    size_t capacity;
} TokenArray;

// AST node types
typedef enum {
    AST_INT,
    AST_STRING,
    AST_CHAR,
    AST_IDENTIFIER,
    AST_LIST,
    AST_ARRAY
} ASTNodeType;

// AST node structure
typedef struct ASTNode {
    ASTNodeType type;
    union {
        int int_value;
        char *string_value;
        char char_value;
        struct {
            struct ASTNode **children;
            size_t count;
            size_t capacity;
        } list;
    } data;
} ASTNode;

// Symbol table entry
typedef enum {
    SYM_INT,
    SYM_STR,
    SYM_CHAR,
    SYM_BOOL,
    SYM_ARRAY,
    SYM_STRUCT,
    SYM_FUNCTION
} SymbolType;

typedef struct {
    char *name;
    SymbolType type;
    union {
        struct {
            SymbolType element_type;
            int size;
        } array;
        struct {
            SymbolType *param_types;
            int param_count;
            SymbolType return_type;
        } function;
    } type_info;
    ASTNode *init_value;
} Symbol;

typedef struct {
    Symbol *symbols;
    size_t count;
    size_t capacity;
} SymbolTable;

// Function prototypes
TokenArray *tokenize(const char *source);
void free_token_array(TokenArray *tokens);
ASTNode *parse(TokenArray *tokens);
void free_ast(ASTNode *node);
SymbolTable *build_symbol_table(ASTNode *ast);
void free_symbol_table(SymbolTable *table);
char *compile_to_arm64(ASTNode *ast, SymbolTable *symbols);

// Utility functions
ASTNode *create_ast_node(ASTNodeType type);
void add_ast_child(ASTNode *parent, ASTNode *child);
void token_array_add(TokenArray *array, Token token);
bool is_keyword(const char *str);
bool is_operator(const char *str);

#endif // TYPES_H