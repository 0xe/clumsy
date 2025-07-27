#ifndef COMPILER_H
#define COMPILER_H

#include "types.h"

// Code generation context
typedef struct {
    char *output;
    size_t output_size;
    size_t output_capacity;
    int label_counter;
} CodeGen;

// Compiler functions
char *compile_to_arm64(ASTNode *ast, SymbolTable *symbols);
SymbolTable *build_symbol_table(ASTNode *ast);
void free_symbol_table(SymbolTable *table);

// Code generation helpers
CodeGen *create_codegen(void);
void free_codegen(CodeGen *codegen);
void emit_code(CodeGen *codegen, const char *format, ...);
char *new_label(CodeGen *codegen, const char *prefix);

// Symbol table helpers
Symbol *find_symbol(SymbolTable *table, const char *name);
void add_symbol(SymbolTable *table, Symbol symbol);
int get_symbol_offset(SymbolTable *table, const char *name);

// AST traversal and code generation
void generate_preamble(CodeGen *codegen);
void generate_main_function(CodeGen *codegen, ASTNode *ast, SymbolTable *symbols);
void generate_function_definitions(CodeGen *codegen, ASTNode *ast, SymbolTable *symbols);
void generate_expression(CodeGen *codegen, ASTNode *expr, SymbolTable *symbols);
void generate_statement(CodeGen *codegen, ASTNode *stmt, SymbolTable *symbols);

// ARM64 instruction helpers
void emit_mov_immediate(CodeGen *codegen, const char *reg, int value);
void emit_load_variable(CodeGen *codegen, const char *reg, const char *var_name, SymbolTable *symbols);
void emit_store_variable(CodeGen *codegen, const char *reg, const char *var_name, SymbolTable *symbols);
void emit_arithmetic(CodeGen *codegen, const char *op, const char *dest, const char *src1, const char *src2);

#endif // COMPILER_H