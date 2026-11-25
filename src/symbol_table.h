#ifndef SYMBOL_TABLE_H
#define SYMBOL_TABLE_H

#include "types.h"

SymbolTable *build_symbol_table(ASTNode *ast);
void free_symbol_table(SymbolTable *table);
Symbol *find_symbol(SymbolTable *table, const char *name);
Symbol *find_symbol_recursive(SymbolTable *table, const char *name);
void add_symbol(SymbolTable *table, Symbol symbol);
int get_symbol_offset(SymbolTable *table, const char *name);
int total_tree_size(SymbolTable *table);
bool ast_uses_exponentiation(ASTNode *node);
SymbolTable *create_child_scope(SymbolTable *parent);
void collect_symbols_from_stmt(ASTNode *stmt, SymbolTable *table);
void collect_symbols_from_block(ASTNode *block, SymbolTable *table);

StructTypeTable *create_struct_type_table(void);
void add_struct_type(StructTypeTable *table, const char *name, StructField *fields, size_t field_count);
StructType *find_struct_type(StructTypeTable *table, const char *name);
void free_struct_type_table(StructTypeTable *table);
StructTypeTable *get_global_struct_types(void);

#endif
