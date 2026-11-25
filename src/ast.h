#ifndef AST_H
#define AST_H

#include <stddef.h>

struct SymbolTable;

typedef enum {
    AST_INT, AST_STRING, AST_CHAR, AST_IDENTIFIER, AST_LIST, AST_ARRAY,
    AST_EXTERN_DECL, AST_FN
} ASTNodeType;

typedef struct ASTNode {
    ASTNodeType type;
    struct SymbolTable *scope;
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

#endif
