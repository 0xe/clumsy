#include "parser.h"
#include "arena.h"

ASTNode *create_ast_node(ASTNodeType type) {
    ASTNode *node = arena_calloc(&g_arena_compile, 1, sizeof(ASTNode));
    node->type = type;
    node->scope = NULL;
    
    if (type == AST_LIST || type == AST_ARRAY || type == AST_EXTERN_DECL) {
        node->data.list.children = NULL;
        node->data.list.count = 0;
        node->data.list.capacity = 0;
    }
    
    return node;
}

void add_ast_child(ASTNode *parent, ASTNode *child) {
    if (parent->type != AST_LIST && parent->type != AST_ARRAY &&
        parent->type != AST_EXTERN_DECL) {
        fprintf(stderr, "Error: Cannot add child to non-list/non-array AST node\n");
        return;
    }
    
    if (parent->data.list.count >= parent->data.list.capacity) {
        size_t old_capacity = parent->data.list.capacity;
        parent->data.list.capacity = old_capacity == 0 ? 4 : old_capacity * 2;
        parent->data.list.children = arena_grow(&g_arena_compile,
                                                parent->data.list.children,
                                                old_capacity * sizeof(ASTNode *),
                                                parent->data.list.capacity * sizeof(ASTNode *));
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
    // consume '['
    next_token(parser);

    ASTNode *array = create_ast_node(AST_ARRAY);

    while (true) {
        while (match_token(parser, TOKEN_NEWLINE) || match_token(parser, TOKEN_COMMENT)) {
            next_token(parser);
        }
        if (match_token(parser, TOKEN_RBRACKET) || match_token(parser, TOKEN_EOF)) break;
        ASTNode *element = parse_exp(parser);
        if (element) {
            add_ast_child(array, element);
        }
    }
    
    if (match_token(parser, TOKEN_RBRACKET)) {
        next_token(parser); // consume ']'
    }
    
    return array;
}

ASTNode *parse_sexp(Parser *parser) {
    // consume '('
    next_token(parser);

    // extern declarations: (extern name [(param types)] return_type)
    if (match_value(parser, "extern")) {
        ASTNode *extern_node = create_ast_node(AST_EXTERN_DECL);
        next_token(parser); // consume 'extern'
        
        // Parse function name
        if (match_token(parser, TOKEN_IDENTIFIER)) {
            ASTNode *name_node = create_ast_node(AST_IDENTIFIER);
            name_node->data.string_value = arena_strdup(&g_arena_compile, current_token(parser)->value);
            add_ast_child(extern_node, name_node);
            next_token(parser);
        }
        
        // Parse parameter list
        if (match_token(parser, TOKEN_LBRACKET)) {
            next_token(parser); // consume '['
            ASTNode *params = create_ast_node(AST_LIST);
            
            while (!match_token(parser, TOKEN_RBRACKET) && !match_token(parser, TOKEN_EOF)) {
                ASTNode *param = parse_exp(parser);
                if (param) {
                    add_ast_child(params, param);
                }
            }
            
            if (match_token(parser, TOKEN_RBRACKET)) {
                next_token(parser); // consume ']'
            }
            add_ast_child(extern_node, params);
        }
        
        // Parse return type
        ASTNode *return_type = parse_exp(parser);
        if (return_type) {
            add_ast_child(extern_node, return_type);
        }
        
        // Skip to closing parenthesis
        while (!match_token(parser, TOKEN_RPAREN) && !match_token(parser, TOKEN_EOF)) {
            next_token(parser);
        }
        
        if (match_token(parser, TOKEN_RPAREN)) {
            next_token(parser); // consume ')'
        }
        
        return extern_node;
    }
    
    ASTNode *list = create_ast_node(AST_LIST);

    // function definitions: (fn [...] ...)
    if (match_value(parser, "fn") && peek_token(parser)->type == TOKEN_LBRACKET) {
        ASTNode *fn_node = create_ast_node(AST_FN);
        fn_node->data.fn.params = NULL;
        fn_node->data.fn.param_count = 0;
        fn_node->data.fn.return_type = NULL;
        fn_node->data.fn.body = NULL;
        next_token(parser); // consume 'fn'
        
        // Parse parameter list
        next_token(parser); // consume '['
        size_t param_cap = 0;
        
        while (!match_token(parser, TOKEN_RBRACKET) && !match_token(parser, TOKEN_EOF)) {
            if (match_token(parser, TOKEN_LPAREN)) {
                ASTNode *param = parse_sexp(parser);
                if (fn_node->data.fn.param_count >= param_cap) {
                    size_t old_cap = param_cap;
                    param_cap = param_cap ? param_cap * 2 : 4;
                    fn_node->data.fn.params = arena_grow(&g_arena_compile,
                                                         fn_node->data.fn.params,
                                                         old_cap * sizeof(ASTNode *),
                                                         param_cap * sizeof(ASTNode *));
                }
                fn_node->data.fn.params[fn_node->data.fn.param_count++] = param;
            } else {
                next_token(parser);
            }
        }
        
        if (match_token(parser, TOKEN_RBRACKET)) {
            next_token(parser); // consume ']'
        }
        
        // Collect return type and body
        ASTNode **extras = NULL;
        size_t extra_count = 0;
        size_t extra_cap = 0;
        while (true) {
            while (match_token(parser, TOKEN_NEWLINE) || match_token(parser, TOKEN_COMMENT)) {
                next_token(parser);
            }
            if (match_token(parser, TOKEN_RPAREN) || match_token(parser, TOKEN_EOF)) break;
            ASTNode *expr = parse_exp(parser);
            if (expr) {
                if (extra_count >= extra_cap) {
                    size_t old_cap = extra_cap;
                    extra_cap = extra_cap ? extra_cap * 2 : 4;
                    extras = arena_grow(&g_arena_compile, extras,
                                        old_cap * sizeof(ASTNode *),
                                        extra_cap * sizeof(ASTNode *));
                }
                extras[extra_count++] = expr;
            }
        }
        // First extra child is return type, rest form the body
        if (extra_count >= 1) {
            fn_node->data.fn.return_type = extras[0];
        }
        if (extra_count >= 2) {
            if (extra_count == 2) {
                fn_node->data.fn.body = extras[1];
            } else {
                // Multiple body expressions — wrap in a begin block
                ASTNode *begin = create_ast_node(AST_LIST);
                ASTNode *begin_op = create_ast_node(AST_IDENTIFIER);
                begin_op->data.string_value = arena_strdup(&g_arena_compile, "begin");
                add_ast_child(begin, begin_op);
                for (size_t i = 1; i < extra_count; i++) {
                    add_ast_child(begin, extras[i]);
                }
                fn_node->data.fn.body = begin;
            }
        }

        if (match_token(parser, TOKEN_RPAREN)) {
            next_token(parser); // consume ')'
        }
        return fn_node;
    } else {
        // s-expression
        while (true) {
            while (match_token(parser, TOKEN_NEWLINE) || match_token(parser, TOKEN_COMMENT)) {
                next_token(parser);
            }
            if (match_token(parser, TOKEN_RPAREN) || match_token(parser, TOKEN_EOF)) break;
            ASTNode *expr = parse_exp(parser);
            if (expr) {
                add_ast_child(list, expr);
            }
        }
    }
    
    if (match_token(parser, TOKEN_RPAREN)) {
        next_token(parser); // consume ')'
    }
    
    return list;
}

ASTNode *parse_exp(Parser *parser) {
    Token *token = current_token(parser);
    
    // ignore comments and newlines
    while (token->type == TOKEN_COMMENT || token->type == TOKEN_NEWLINE) {
        next_token(parser);
        token = current_token(parser);
    }
    
    if (token->type == TOKEN_EOF) {
        return NULL;
    }
    
    switch (token->type) {
        case TOKEN_LPAREN:
            return parse_sexp(parser);
            
        case TOKEN_LBRACKET:
            return parse_array_literal(parser);
            
        case TOKEN_INT: {
            ASTNode *node = create_ast_node(AST_INT);
            node->data.int_value = (int)strtol(token->value, NULL, 0);
            next_token(parser);
            return node;
        }
        
        case TOKEN_STRING: {
            ASTNode *node = create_ast_node(AST_STRING);
            node->data.string_value = arena_strdup(&g_arena_compile, token->value);
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
            // struct literals: #((field value)...)
            if (match_value(parser, "#") && peek_token(parser)->type == TOKEN_LPAREN) {
                next_token(parser); // consume '#'
                
                ASTNode *struct_literal = create_ast_node(AST_LIST);
                
                // mark struct literals
                ASTNode *struct_op = create_ast_node(AST_IDENTIFIER);
                struct_op->data.string_value = arena_strdup(&g_arena_compile, "#");
                add_ast_child(struct_literal, struct_op);
                
                // struct fields
                ASTNode *fields = parse_sexp(parser);
                if (fields) {
                    add_ast_child(struct_literal, fields);
                }
                
                return struct_literal;
            }
            ASTNode *node = create_ast_node(AST_IDENTIFIER);
            node->data.string_value = arena_strdup(&g_arena_compile, token->value);
            next_token(parser);
            
            // array indexing: identifier[expression]
            if (match_token(parser, TOKEN_LBRACKET)) {
                next_token(parser); // consume '['
                
                ASTNode *index_expr = create_ast_node(AST_LIST);
                
                // array access operator
                ASTNode *access_op = create_ast_node(AST_IDENTIFIER);
                access_op->data.string_value = arena_strdup(&g_arena_compile, "[]");
                add_ast_child(index_expr, access_op);
                
                // array variable
                add_ast_child(index_expr, node);
                
                // index expression
                ASTNode *index = parse_exp(parser);
                if (index) {
                    add_ast_child(index_expr, index);
                }
                
                if (match_token(parser, TOKEN_RBRACKET)) {
                    next_token(parser); // consume ']'
                }
                
                return index_expr;
            }
            
            // field access: identifier.field
            if (match_value(parser, ".")) {
                next_token(parser); // consume '.'
                
                if (match_token(parser, TOKEN_IDENTIFIER)) {
                    ASTNode *field_access = create_ast_node(AST_LIST);
                    
                    // field access operator
                    ASTNode *access_op = create_ast_node(AST_IDENTIFIER);
                    access_op->data.string_value = arena_strdup(&g_arena_compile, ".");
                    add_ast_child(field_access, access_op);
                    
                    // struct variable
                    add_ast_child(field_access, node);
                    
                    // field name
                    ASTNode *field = create_ast_node(AST_IDENTIFIER);
                    field->data.string_value = arena_strdup(&g_arena_compile, current_token(parser)->value);
                    add_ast_child(field_access, field);
                    next_token(parser);
                    
                    return field_access;
                }
            }
            
            // array type: type[size]
            if (match_token(parser, TOKEN_LBRACKET)) {
                next_token(parser); // consume '['
                
                ASTNode *array_type = create_ast_node(AST_LIST);
                
                // array type operator
                ASTNode *type_op = create_ast_node(AST_IDENTIFIER);
                type_op->data.string_value = arena_strdup(&g_arena_compile, "array_type");
                add_ast_child(array_type, type_op);
                
                // base type
                add_ast_child(array_type, node);
                
                // size expression
                ASTNode *size = parse_exp(parser);
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
            // quote - create a list with 'quote' and the next expression
            next_token(parser); // consume 'quote'
            ASTNode *quote_list = create_ast_node(AST_LIST);
            
            ASTNode *quote_symbol = create_ast_node(AST_IDENTIFIER);
            quote_symbol->data.string_value = arena_strdup(&g_arena_compile, "quote");
            add_ast_child(quote_list, quote_symbol);
            
            ASTNode *quoted_expr = parse_exp(parser);
            if (quoted_expr) {
                add_ast_child(quote_list, quoted_expr);
            }
            
            return quote_list;
        }
        
        default:
            fprintf(stderr, "syntax error: unexpected token '%s' at line %d\n",
                    token->value ? token->value : "(null)", token->line);
            exit(1);
    }
}

ASTNode *parse(TokenArray *tokens) {
    Parser parser;
    parser.tokens = tokens;
    parser.current = 0;
    
    ASTNode *root = create_ast_node(AST_LIST);
    
    while (!match_token(&parser, TOKEN_EOF)) {
        ASTNode *expr = parse_exp(&parser);
        if (expr) {
            add_ast_child(root, expr);
        }
    }
    
    return root;
}

void free_ast(ASTNode *node) {
    (void)node;
}

void print_symbol_table(SymbolTable *symbols) {
    if (!symbols) {
        fprintf(stderr, "  (null symbol table)\n");
        return;
    }
    
    if (symbols->count == 0) {
        fprintf(stderr, "  (empty symbol table)\n");
        return;
    }
    
    for (size_t i = 0; i < symbols->count; i++) {
        Symbol *sym = &symbols->symbols[i];
        fprintf(stderr, "  %s: ", sym->name);
        
        switch (sym->type) {
            case SYM_INT:
                fprintf(stderr, "int");
                break;
            case SYM_STR:
                fprintf(stderr, "str");
                break;
            case SYM_CHAR:
                fprintf(stderr, "char");
                break;
            case SYM_BOOL:
                fprintf(stderr, "bool");
                break;
            case SYM_POINTER:
                fprintf(stderr, "pointer");
                break;
            case SYM_C_FUNCTION:
                fprintf(stderr, "c_function");
                break;
            case SYM_ARRAY:
                fprintf(stderr, "array[%d] of ", sym->type_info.array.size);
                switch (sym->type_info.array.element_type) {
                    case SYM_INT: fprintf(stderr, "int"); break;
                    case SYM_STR: fprintf(stderr, "str"); break;
                    case SYM_CHAR: fprintf(stderr, "char"); break;
                    case SYM_BOOL: fprintf(stderr, "bool"); break;
                    default: fprintf(stderr, "unknown"); break;
                }
                break;
            case SYM_STRUCT:
                fprintf(stderr, "struct %s", sym->type_info.struct_instance.struct_type_name);
                break;
            case SYM_FUNCTION:
                fprintf(stderr, "fn(");
                for (int j = 0; j < sym->type_info.function.param_count; j++) {
                    if (j > 0) fprintf(stderr, ", ");
                    switch (sym->type_info.function.param_types[j]) {
                        case SYM_INT: fprintf(stderr, "int"); break;
                        case SYM_STR: fprintf(stderr, "str"); break;
                        case SYM_CHAR: fprintf(stderr, "char"); break;
                        case SYM_BOOL: fprintf(stderr, "bool"); break;
                        default: fprintf(stderr, "unknown"); break;
                    }
                }
                fprintf(stderr, ") -> ");
                switch (sym->type_info.function.return_type) {
                    case SYM_INT: fprintf(stderr, "int"); break;
                    case SYM_STR: fprintf(stderr, "str"); break;
                    case SYM_CHAR: fprintf(stderr, "char"); break;
                    case SYM_BOOL: fprintf(stderr, "bool"); break;
                    default: fprintf(stderr, "unknown"); break;
                }
                break;
            default:
                fprintf(stderr, "unknown_type(%d)", sym->type);
                break;
        }
        fprintf(stderr, "\n");
    }
}

void print_ast(ASTNode *node, int indent) {
    if (!node) {
        fprintf(stderr, "%*sNULL\n", indent, "");
        return;
    }
    
    switch (node->type) {
        case AST_INT:
            fprintf(stderr, "%*sINT: %d\n", indent, "", node->data.int_value);
            break;
        case AST_CHAR:
            fprintf(stderr, "%*sCHAR: '%c'\n", indent, "", node->data.char_value);
            break;
        case AST_IDENTIFIER:
            fprintf(stderr, "%*sIDENTIFIER: %s\n", indent, "", node->data.string_value);
            break;
        case AST_STRING:
            fprintf(stderr, "%*sSTRING: \"%s\"\n", indent, "", node->data.string_value);
            break;
        case AST_LIST:
            fprintf(stderr, "%*sLIST (%zu children):\n", indent, "", node->data.list.count);
            for (size_t i = 0; i < node->data.list.count; i++) {
                print_ast(node->data.list.children[i], indent + 2);
            }
            break;
        case AST_ARRAY:
            fprintf(stderr, "%*sARRAY (%zu elements):\n", indent, "", node->data.list.count);
            for (size_t i = 0; i < node->data.list.count; i++) {
                print_ast(node->data.list.children[i], indent + 2);
            }
            break;
        case AST_EXTERN_DECL:
            fprintf(stderr, "%*sFUNCTION (%zu elements):\n", indent, "", node->data.list.count);
            break;
        case AST_FN:
            fprintf(stderr, "%*sFN (%zu params):\n", indent, "", node->data.fn.param_count);
            for (size_t i = 0; i < node->data.fn.param_count; i++) {
                print_ast(node->data.fn.params[i], indent + 2);
            }
            if (node->data.fn.return_type) {
                fprintf(stderr, "%*s  return type:\n", indent, "");
                print_ast(node->data.fn.return_type, indent + 4);
            }
            if (node->data.fn.body) {
                fprintf(stderr, "%*s  body:\n", indent, "");
                print_ast(node->data.fn.body, indent + 4);
            }
            break;
        default:
            fprintf(stderr, "%*sUNKNOWN TYPE: %d\n", indent, "", node->type);
            break;
    }
}