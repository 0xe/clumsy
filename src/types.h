#ifndef TYPES_H
#define TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef enum {
    TOKEN_LPAREN,           // (
    TOKEN_RPAREN,           // )
    TOKEN_LBRACKET,         // [
    TOKEN_RBRACKET,         // ]
    TOKEN_QUOTE,            // '
    TOKEN_INT,              // [0-9]*
    TOKEN_STRING,           // [a-zA-Z0-9]*
    TOKEN_CHAR,             // [a-z]?
    TOKEN_IDENTIFIER,       // id
    TOKEN_KEYWORD,          // keywords (struct etc.)
    TOKEN_OPERATOR,         // +,-,*,/ etc.
    TOKEN_COMMENT,          //
    TOKEN_NEWLINE,          // \n
    TOKEN_EXTERN,           // extern
    TOKEN_EOF
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

typedef enum {
    AST_INT,
    AST_STRING,
    AST_CHAR,
    AST_IDENTIFIER,
    AST_LIST,
    AST_ARRAY,
    AST_EXTERN_DECL,
    AST_ADDRESS_OF,
    AST_DEREFERENCE,
    AST_FN
} ASTNodeType;

typedef struct ASTNode {
    ASTNodeType type;
    struct SymbolTable *scope;  // For begin/if/while: child scope (set by build_symbol_table)
    union {
        int int_value;
        char *string_value;
        char char_value;
        struct {
            struct ASTNode **children;
            size_t count;
            size_t capacity;
        } list;
        struct {
            struct ASTNode **params;
            size_t param_count;
            struct ASTNode *return_type;
            struct ASTNode *body;
        } fn;
    } data;
} ASTNode;

typedef enum {
    SYM_INT,
    SYM_STR,
    SYM_CHAR,
    SYM_BOOL,
    SYM_ARRAY,
    SYM_STRUCT,
    SYM_FUNCTION,
    SYM_POINTER,
    SYM_C_FUNCTION
} SymbolType;

typedef struct {
    char *name;
    SymbolType type;
    bool is_external;   // hack for C functions (type == SYM_C_FUNCTION should be enough)
    char *c_library;    // library name for external functions
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
        struct {
            char *struct_type_name;
        } struct_instance;
        struct {
            SymbolType pointed_type;
        } pointer;
        struct {
            char *c_name;      // actual C function name
            SymbolType *param_types;
            int param_count;
            SymbolType return_type;
        } c_function;
    } type_info;
    ASTNode *init_value;
    int stack_offset;  // absolute offset from sp within the current stack frame
} Symbol;

typedef struct {
    char *name;
    SymbolType type;
    ASTNode *default_value;
} StructField;

typedef struct {
    char *name;
    StructField *fields;
    size_t count;
    size_t capacity;
} StructType;

typedef struct {
    StructType *types;
    size_t count;
    size_t capacity;
} StructTypeTable;

typedef struct SymbolTable {
    Symbol *symbols;
    size_t count;
    size_t capacity;
    struct SymbolTable *parent; // parent scope lookup
    struct SymbolTable *first_child;
    struct SymbolTable *next_sibling;
    int next_offset;  // next available stack offset for non-function symbols
} SymbolTable;

TokenArray *tokenize(const char *source);
void free_token_array(TokenArray *tokens);

ASTNode *parse(TokenArray *tokens);
void free_ast(ASTNode *node);

SymbolTable *build_symbol_table(ASTNode *ast);
void free_symbol_table(SymbolTable *table);

StructTypeTable *create_struct_type_table();
void add_struct_type(StructTypeTable *table, const char *name, StructField *fields, size_t field_count);
StructType *find_struct_type(StructTypeTable *table, const char *name);
void free_struct_type_table(StructTypeTable *table);

char *compile_to_arm64(ASTNode *ast, SymbolTable *symbols, StructTypeTable *struct_types);
Symbol *find_symbol_recursive(SymbolTable *table, const char *name);

ASTNode *create_ast_node(ASTNodeType type);
void add_ast_child(ASTNode *parent, ASTNode *child);
void token_array_add(TokenArray *array, Token token);
bool is_keyword(const char *str);
bool is_operator(const char *str);

#endif // TYPES_H