#include "parser.h"

ASTNode *create_ast_node(ASTNodeType type) {
    ASTNode *node = malloc(sizeof(ASTNode));
    if (!node) {
        fprintf(stderr, "Error: Failed to allocate memory for AST node\n");
        exit(1);
    }
    node->type = type;
    
    if (type == AST_LIST || type == AST_ARRAY) {
        node->data.list.children = NULL;
        node->data.list.count = 0;
        node->data.list.capacity = 0;
    }
    
    return node;
}

void add_ast_child(ASTNode *parent, ASTNode *child) {
    if (parent->type != AST_LIST && parent->type != AST_ARRAY) {
        fprintf(stderr, "Error: Cannot add child to non-list/non-array AST node\n");
        return;
    }
    
    if (parent->data.list.count >= parent->data.list.capacity) {
        parent->data.list.capacity = parent->data.list.capacity == 0 ? 4 : parent->data.list.capacity * 2;
        parent->data.list.children = realloc(parent->data.list.children, 
                                            parent->data.list.capacity * sizeof(ASTNode*));
        if (!parent->data.list.children) {
            fprintf(stderr, "Error: Failed to allocate memory for AST children\n");
            exit(1);
        }
    }
    
    parent->data.list.children[parent->data.list.count++] = child;
}

Token *current_token(Parser *parser) {
    if (parser->current >= parser->tokens->count) {
        return &parser->tokens->tokens[parser->tokens->count - 1]; // EOF token
    }
    return &parser->tokens->tokens[parser->current];
}

Token *next_token(Parser *parser) {
    if (parser->current < parser->tokens->count - 1) {
        parser->current++;
    }
    return current_token(parser);
}

Token *peek_token(Parser *parser) {
    if (parser->current + 1 >= parser->tokens->count) {
        return &parser->tokens->tokens[parser->tokens->count - 1]; // EOF token
    }
    return &parser->tokens->tokens[parser->current + 1];
}

bool match_token(Parser *parser, TokenType type) {
    return current_token(parser)->type == type;
}

bool match_value(Parser *parser, const char *value) {
    Token *token = current_token(parser);
    return token->value && strcmp(token->value, value) == 0;
}

ASTNode *parse_array_literal(Parser *parser) {
    // Consume '['
    next_token(parser);
    
    ASTNode *array = create_ast_node(AST_ARRAY);
    
    while (!match_token(parser, TOKEN_RBRACKET) && !match_token(parser, TOKEN_EOF)) {
        ASTNode *element = parse_expression(parser);
        if (element) {
            add_ast_child(array, element);
        }
    }
    
    if (match_token(parser, TOKEN_RBRACKET)) {
        next_token(parser); // Consume ']'
    }
    
    return array;
}

ASTNode *parse_s_expression(Parser *parser) {
    // Consume '('
    next_token(parser);
    
    ASTNode *list = create_ast_node(AST_LIST);
    
    // Handle function definitions: (fn [...] ...)
    if (match_value(parser, "fn") && peek_token(parser)->type == TOKEN_LBRACKET) {
        // Add 'fn' token
        ASTNode *fn_node = create_ast_node(AST_IDENTIFIER);
        fn_node->data.string_value = strdup("fn");
        add_ast_child(list, fn_node);
        next_token(parser); // consume 'fn'
        
        // Parse parameter list
        next_token(parser); // consume '['
        ASTNode *params = create_ast_node(AST_LIST);
        
        while (!match_token(parser, TOKEN_RBRACKET) && !match_token(parser, TOKEN_EOF)) {
            if (match_token(parser, TOKEN_LPAREN)) {
                // Parameter: (name type)
                ASTNode *param = parse_s_expression(parser);
                add_ast_child(params, param);
            } else {
                next_token(parser);
            }
        }
        
        if (match_token(parser, TOKEN_RBRACKET)) {
            next_token(parser); // consume ']'
        }
        add_ast_child(list, params);
        
        // Parse return type and body
        while (!match_token(parser, TOKEN_RPAREN) && !match_token(parser, TOKEN_EOF)) {
            ASTNode *expr = parse_expression(parser);
            if (expr) {
                add_ast_child(list, expr);
            }
        }
    } else {
        // Regular s-expression
        while (!match_token(parser, TOKEN_RPAREN) && !match_token(parser, TOKEN_EOF)) {
            ASTNode *expr = parse_expression(parser);
            if (expr) {
                add_ast_child(list, expr);
            }
        }
    }
    
    if (match_token(parser, TOKEN_RPAREN)) {
        next_token(parser); // Consume ')'
    }
    
    return list;
}

ASTNode *parse_expression(Parser *parser) {
    Token *token = current_token(parser);
    
    // Skip comments and newlines
    while (token->type == TOKEN_COMMENT || token->type == TOKEN_NEWLINE) {
        next_token(parser);
        token = current_token(parser);
    }
    
    if (token->type == TOKEN_EOF) {
        return NULL;
    }
    
    switch (token->type) {
        case TOKEN_LPAREN:
            return parse_s_expression(parser);
            
        case TOKEN_LBRACKET:
            return parse_array_literal(parser);
            
        case TOKEN_INT: {
            ASTNode *node = create_ast_node(AST_INT);
            node->data.int_value = atoi(token->value);
            next_token(parser);
            return node;
        }
        
        case TOKEN_STRING: {
            ASTNode *node = create_ast_node(AST_STRING);
            node->data.string_value = strdup(token->value);
            next_token(parser);
            return node;
        }
        
        case TOKEN_CHAR: {
            ASTNode *node = create_ast_node(AST_CHAR);
            // Extract character from #\c format
            if (strlen(token->value) >= 3) {
                node->data.char_value = token->value[2];
            } else {
                node->data.char_value = '\0';
            }
            next_token(parser);
            return node;
        }
        
        case TOKEN_IDENTIFIER:
        case TOKEN_KEYWORD:
        case TOKEN_OPERATOR: {
            // Handle struct literals: #((field value)...)
            if (match_value(parser, "#") && peek_token(parser)->type == TOKEN_LPAREN) {
                next_token(parser); // consume '#'
                
                ASTNode *struct_literal = create_ast_node(AST_LIST);
                
                // Add struct literal marker
                ASTNode *struct_op = create_ast_node(AST_IDENTIFIER);
                struct_op->data.string_value = strdup("#");
                add_ast_child(struct_literal, struct_op);
                
                // Parse the struct fields
                ASTNode *fields = parse_s_expression(parser);
                if (fields) {
                    add_ast_child(struct_literal, fields);
                }
                
                return struct_literal;
            }
            ASTNode *node = create_ast_node(AST_IDENTIFIER);
            node->data.string_value = strdup(token->value);
            next_token(parser);
            
            // Check for array indexing: identifier[expression]
            if (match_token(parser, TOKEN_LBRACKET)) {
                next_token(parser); // consume '['
                
                ASTNode *index_expr = create_ast_node(AST_LIST);
                
                // Add array access operator
                ASTNode *access_op = create_ast_node(AST_IDENTIFIER);
                access_op->data.string_value = strdup("[]");
                add_ast_child(index_expr, access_op);
                
                // Add the array variable
                add_ast_child(index_expr, node);
                
                // Parse the index expression
                ASTNode *index = parse_expression(parser);
                if (index) {
                    add_ast_child(index_expr, index);
                }
                
                if (match_token(parser, TOKEN_RBRACKET)) {
                    next_token(parser); // consume ']'
                }
                
                return index_expr;
            }
            
            // Check for field access: identifier.field
            if (match_value(parser, ".")) {
                next_token(parser); // consume '.'
                
                if (match_token(parser, TOKEN_IDENTIFIER)) {
                    ASTNode *field_access = create_ast_node(AST_LIST);
                    
                    // Add field access operator
                    ASTNode *access_op = create_ast_node(AST_IDENTIFIER);
                    access_op->data.string_value = strdup(".");
                    add_ast_child(field_access, access_op);
                    
                    // Add the struct variable
                    add_ast_child(field_access, node);
                    
                    // Add the field name
                    ASTNode *field = create_ast_node(AST_IDENTIFIER);
                    field->data.string_value = strdup(current_token(parser)->value);
                    add_ast_child(field_access, field);
                    next_token(parser);
                    
                    return field_access;
                }
            }
            
            // Check for array type: type[size]
            if (match_token(parser, TOKEN_LBRACKET)) {
                next_token(parser); // consume '['
                
                ASTNode *array_type = create_ast_node(AST_LIST);
                
                // Add array type operator
                ASTNode *type_op = create_ast_node(AST_IDENTIFIER);
                type_op->data.string_value = strdup("array_type");
                add_ast_child(array_type, type_op);
                
                // Add the base type
                add_ast_child(array_type, node);
                
                // Parse the size expression
                ASTNode *size = parse_expression(parser);
                if (size) {
                    add_ast_child(array_type, size);
                }
                
                if (match_token(parser, TOKEN_RBRACKET)) {
                    next_token(parser); // consume ']'
                }
                
                return array_type;
            }
            
            return node;
        }
        
        case TOKEN_QUOTE: {
            // Handle quote specially - create a list with 'quote' and the next expression
            next_token(parser); // consume 'quote'
            ASTNode *quote_list = create_ast_node(AST_LIST);
            
            ASTNode *quote_symbol = create_ast_node(AST_IDENTIFIER);
            quote_symbol->data.string_value = strdup("quote");
            add_ast_child(quote_list, quote_symbol);
            
            ASTNode *quoted_expr = parse_expression(parser);
            if (quoted_expr) {
                add_ast_child(quote_list, quoted_expr);
            }
            
            return quote_list;
        }
        
        default:
            // Skip unknown tokens
            next_token(parser);
            return parse_expression(parser);
    }
}

ASTNode *parse(TokenArray *tokens) {
    Parser parser;
    parser.tokens = tokens;
    parser.current = 0;
    
    ASTNode *root = create_ast_node(AST_LIST);
    
    while (!match_token(&parser, TOKEN_EOF)) {
        ASTNode *expr = parse_expression(&parser);
        if (expr) {
            add_ast_child(root, expr);
        }
    }
    
    return root;
}

void free_ast(ASTNode *node) {
    if (!node) return;
    
    switch (node->type) {
        case AST_STRING:
        case AST_IDENTIFIER:
            free(node->data.string_value);
            break;
            
        case AST_LIST:
        case AST_ARRAY:
            for (size_t i = 0; i < node->data.list.count; i++) {
                free_ast(node->data.list.children[i]);
            }
            free(node->data.list.children);
            break;
            
        case AST_INT:
        case AST_CHAR:
            // No dynamic memory to free
            break;
    }
    
    free(node);
}

void print_ast(ASTNode *node, int indent) {
    if (!node) {
        printf("%*sNULL\n", indent, "");
        return;
    }
    
    switch (node->type) {
        case AST_INT:
            printf("%*sINT: %d\n", indent, "", node->data.int_value);
            break;
        case AST_CHAR:
            printf("%*sCHAR: '%c'\n", indent, "", node->data.char_value);
            break;
        case AST_IDENTIFIER:
            printf("%*sIDENTIFIER: %s\n", indent, "", node->data.string_value);
            break;
        case AST_STRING:
            printf("%*sSTRING: \"%s\"\n", indent, "", node->data.string_value);
            break;
        case AST_LIST:
            printf("%*sLIST (%zu children):\n", indent, "", node->data.list.count);
            for (size_t i = 0; i < node->data.list.count; i++) {
                print_ast(node->data.list.children[i], indent + 2);
            }
            break;
        case AST_ARRAY:
            printf("%*sARRAY (%zu elements):\n", indent, "", node->data.list.count);
            for (size_t i = 0; i < node->data.list.count; i++) {
                print_ast(node->data.list.children[i], indent + 2);
            }
            break;
        default:
            printf("%*sUNKNOWN TYPE: %d\n", indent, "", node->type);
            break;
    }
}