#ifndef SYMBOL_H
#define SYMBOL_H

#include <stdbool.h>
#include <stddef.h>

#ifndef AST_H
typedef struct ASTNode ASTNode;
#endif

typedef enum {
    SYM_INT, SYM_STR, SYM_CHAR, SYM_BOOL, SYM_ARRAY, SYM_STRUCT,
    SYM_FUNCTION, SYM_POINTER, SYM_C_FUNCTION
} SymbolType;

typedef struct {
    char *name;
    SymbolType type;
    bool is_external;
    bool is_public;
    bool is_global;
    char *c_library;
    union {
        struct { SymbolType element_type; int size; } array;
        struct { SymbolType *param_types; int param_count; SymbolType return_type; } function;
        struct { char *struct_type_name; } struct_instance;
        struct { SymbolType pointed_type; } pointer;
        struct { char *c_name; SymbolType *param_types; int param_count; SymbolType return_type; } c_function;
    } type_info;
    ASTNode *init_value;
    int stack_offset;
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
    struct SymbolTable *parent;
    struct SymbolTable *first_child;
    struct SymbolTable *next_sibling;
    int next_offset;
} SymbolTable;

#endif
