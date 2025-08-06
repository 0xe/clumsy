#ifndef PARSER_H
#define PARSER_H

#include "types.h"

typedef struct {
    TokenArray *tokens;
    size_t current;
} Parser;

ASTNode *parse(TokenArray *tokens);
void free_ast(ASTNode *node);

ASTNode *create_ast_node(ASTNodeType type);
void add_ast_child(ASTNode *parent, ASTNode *child);
ASTNode *parse_exp(Parser *parser);
ASTNode *parse_sexp(Parser *parser);
ASTNode *parse_array_literal(Parser *parser);

Token *current_token(Parser *parser);
Token *next_token(Parser *parser);
Token *peek_token(Parser *parser);
bool match_token(Parser *parser, TokenType type);
bool match_value(Parser *parser, const char *value);

void print_ast(ASTNode *node, int indent);
void print_symbol_table(SymbolTable *symbols);

#endif // PARSER_H