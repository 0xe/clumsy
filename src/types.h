#ifndef TYPES_H
#define TYPES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "token.h"
#include "ast.h"
#include "symbol.h"

TokenArray *tokenize(const char *source);
void free_token_array(TokenArray *tokens);

ASTNode *parse(TokenArray *tokens);
void free_ast(ASTNode *node);

SymbolTable *build_symbol_table(ASTNode *ast);
void free_symbol_table(SymbolTable *table);

StructTypeTable *create_struct_type_table(void);
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
