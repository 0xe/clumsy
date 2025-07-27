#ifndef PARSER_H
#define PARSER_H

#include "types.h"

// Parser state
typedef struct {
    TokenArray *tokens;
    size_t current;
} Parser;

// Parser functions
ASTNode *parse(TokenArray *tokens);
void free_ast(ASTNode *node);

// Helper functions
ASTNode *create_ast_node(ASTNodeType type);
void add_ast_child(ASTNode *parent, ASTNode *child);
ASTNode *parse_expression(Parser *parser);
ASTNode *parse_s_expression(Parser *parser);
ASTNode *parse_array_literal(Parser *parser);

// Parser utilities
Token *current_token(Parser *parser);
Token *next_token(Parser *parser);
Token *peek_token(Parser *parser);
bool match_token(Parser *parser, TokenType type);
bool match_value(Parser *parser, const char *value);

#endif // PARSER_H