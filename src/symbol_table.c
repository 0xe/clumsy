#include "symbol_table.h"
#include "arena.h"
#include "vec.h"

static StructTypeTable *global_struct_types = NULL;

StructTypeTable *get_global_struct_types(void) {
    return global_struct_types;
}

SymbolTable *create_child_scope(SymbolTable *parent) {
    SymbolTable *child = arena_calloc(&g_arena_compile, 1, sizeof(SymbolTable));
    child->parent = parent;
    child->next_offset = parent->next_offset;
    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        SymbolTable *sibling = parent->first_child;
        while (sibling->next_sibling) sibling = sibling->next_sibling;
        sibling->next_sibling = child;
    }
    return child;
}

void collect_symbols_from_stmt(ASTNode *stmt, SymbolTable *table);

void collect_symbols_from_block(ASTNode *block, SymbolTable *table) {
    if (!block || block->type != AST_LIST) return;
    for (size_t i = 0; i < block->data.list.count; i++) {
        collect_symbols_from_stmt(block->data.list.children[i], table);
    }
}

StructTypeTable *create_struct_type_table(void) {
    StructTypeTable *t = arena_calloc(&g_arena_compile, 1, sizeof(StructTypeTable));
    if (!global_struct_types) global_struct_types = t;
    return t;
}

void add_struct_type(StructTypeTable *table, const char *name, StructField *fields, size_t field_count) {
    VEC_GROW(table->types, table->count, table->capacity, StructType, &g_arena_compile);
    StructType *type = &table->types[table->count];
    type->name = arena_strdup(&g_arena_compile, name);
    type->fields = arena_calloc(&g_arena_compile, field_count, sizeof(StructField));
    type->count = field_count;
    type->capacity = field_count;
    for (size_t i = 0; i < field_count; i++) {
        type->fields[i].name = fields[i].name ? arena_strdup(&g_arena_compile, fields[i].name) : NULL;
        type->fields[i].type = fields[i].type;
        type->fields[i].default_value = fields[i].default_value;
    }
    table->count++;
}

StructType *find_struct_type(StructTypeTable *table, const char *name) {
    if (!table) return NULL;
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->types[i].name, name) == 0) return &table->types[i];
    }
    return NULL;
}

void free_struct_type_table(StructTypeTable *table) { (void)table; }

static int symbol_stack_size(Symbol *sym) {
    if (sym->type == SYM_ARRAY) {
        int size = sym->type_info.array.size;
        if (size <= 0) size = 4;
        return size * 8;
    }
    if (sym->type == SYM_STRUCT) {
        if (global_struct_types && sym->type_info.struct_instance.struct_type_name) {
            StructType *st = find_struct_type(global_struct_types, sym->type_info.struct_instance.struct_type_name);
            if (st) return st->count * 8;
        }
        return 16;
    }
    return 8;
}

void add_symbol(SymbolTable *table, Symbol symbol) {
    VEC_GROW(table->symbols, table->count, table->capacity, Symbol, &g_arena_compile);
    if (symbol.is_global || symbol.is_external) {
        symbol.stack_offset = -1;
    } else if (symbol.type != SYM_FUNCTION && symbol.type != SYM_C_FUNCTION) {
        symbol.stack_offset = table->next_offset;
        table->next_offset += symbol_stack_size(&symbol);
    } else {
        symbol.stack_offset = -1;
    }
    table->symbols[table->count++] = symbol;
}

Symbol *find_symbol(SymbolTable *table, const char *name) {
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->symbols[i].name, name) == 0) return &table->symbols[i];
    }
    return NULL;
}

Symbol *find_symbol_recursive(SymbolTable *table, const char *name) {
    if (!table) return NULL;
    Symbol *symbol = find_symbol(table, name);
    if (symbol) return symbol;
    if (table->parent) return find_symbol_recursive(table->parent, name);
    return NULL;
}

static int total_scope_size(SymbolTable *table) {
    int size = 0;
    for (size_t i = 0; i < table->count; i++) {
        Symbol *sym = &table->symbols[i];
        if (sym->type == SYM_FUNCTION || sym->type == SYM_C_FUNCTION) continue;
        if (sym->is_global || sym->is_external) continue;
        size += symbol_stack_size(sym);
    }
    return size;
}

int total_tree_size(SymbolTable *table) {
    if (!table) return 0;
    int size = total_scope_size(table);
    for (SymbolTable *child = table->first_child; child; child = child->next_sibling) {
        size += total_tree_size(child);
    }
    return size;
}

int get_symbol_offset(SymbolTable *table, const char *name) {
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->symbols[i].name, name) == 0) return table->symbols[i].stack_offset;
    }
    if (table->parent) return get_symbol_offset(table->parent, name);
    return -1;
}

void free_symbol_table(SymbolTable *table) { (void)table; }

void collect_symbols_from_stmt(ASTNode *stmt, SymbolTable *table) {
    if (!stmt) return;

    if (stmt->type == AST_EXTERN_DECL) {
        ASTNode *name_node = stmt->data.list.children[0];
        Symbol symbol = {0};
        symbol.name = arena_strdup(&g_arena_compile, name_node->data.string_value);
        symbol.is_external = true;
        symbol.is_global = true;

        if (stmt->data.list.count >= 3) {
            ASTNode *params_node = stmt->data.list.children[1];
            symbol.type = SYM_C_FUNCTION;
            symbol.type_info.c_function.c_name = arena_strdup(&g_arena_compile, name_node->data.string_value);
            symbol.type_info.c_function.return_type = SYM_INT;
            symbol.type_info.c_function.param_count = params_node->data.list.count;
        } else {
            symbol.type = SYM_INT;
            if (stmt->data.list.count >= 2) {
                ASTNode *type_node = stmt->data.list.children[1];
                if (type_node->type == AST_IDENTIFIER) {
                    if (strcmp(type_node->data.string_value, "int") == 0) symbol.type = SYM_INT;
                    else if (strcmp(type_node->data.string_value, "str") == 0) symbol.type = SYM_STR;
                    else if (strcmp(type_node->data.string_value, "char") == 0) symbol.type = SYM_CHAR;
                } else if (type_node->type == AST_LIST && type_node->data.list.count >= 2) {
                    ASTNode *type_op = type_node->data.list.children[0];
                    if (type_op->type == AST_IDENTIFIER && strcmp(type_op->data.string_value, "*") == 0) {
                        symbol.type = SYM_POINTER;
                    } else if (type_op->type == AST_IDENTIFIER &&
                               (strcmp(type_op->data.string_value, "[]") == 0 ||
                                strcmp(type_op->data.string_value, "array_type") == 0)) {
                        symbol.type = SYM_ARRAY;
                        if (type_node->data.list.count >= 3) {
                            ASTNode *size_node = type_node->data.list.children[2];
                            if (size_node->type == AST_INT) symbol.type_info.array.size = size_node->data.int_value;
                        }
                    }
                }
            }
        }
        add_symbol(table, symbol);
        return;
    }

    if (stmt->type != AST_LIST || stmt->data.list.count < 3) return;
    ASTNode *op = stmt->data.list.children[0];
    if (op->type != AST_IDENTIFIER) return;

    if (strcmp(op->data.string_value, "let") == 0 || strcmp(op->data.string_value, "pub") == 0) {
        bool is_pub = strcmp(op->data.string_value, "pub") == 0;
        ASTNode *name_node = stmt->data.list.children[1];
        if (name_node->type != AST_IDENTIFIER) return;

        Symbol symbol = {0};
        symbol.name = arena_strdup(&g_arena_compile, name_node->data.string_value);
        symbol.is_public = is_pub;
        symbol.is_global = (table->parent == NULL);

        if (stmt->data.list.count == 3) {
            symbol.type = SYM_INT;
        } else if (stmt->data.list.count >= 4) {
            ASTNode *type_node = stmt->data.list.children[2];
            if (type_node->type == AST_IDENTIFIER) {
                if (strcmp(type_node->data.string_value, "int") == 0) symbol.type = SYM_INT;
                else if (strcmp(type_node->data.string_value, "str") == 0) symbol.type = SYM_STR;
                else if (strcmp(type_node->data.string_value, "char") == 0) symbol.type = SYM_CHAR;
                else if (strcmp(type_node->data.string_value, "bool") == 0) symbol.type = SYM_BOOL;
                else if (strcmp(type_node->data.string_value, "struct") == 0) return;
                else {
                    symbol.type = SYM_STRUCT;
                    symbol.type_info.struct_instance.struct_type_name = arena_strdup(&g_arena_compile, type_node->data.string_value);
                }
            } else if (type_node->type == AST_LIST && type_node->data.list.count >= 2) {
                ASTNode *type_op = type_node->data.list.children[0];
                if (type_op->type == AST_IDENTIFIER &&
                    (strcmp(type_op->data.string_value, "array_type") == 0 ||
                     strcmp(type_op->data.string_value, "[]") == 0)) {
                    symbol.type = SYM_ARRAY;
                    if (type_node->data.list.count >= 3) {
                        ASTNode *size_node = type_node->data.list.children[2];
                        if (size_node->type == AST_INT) symbol.type_info.array.size = size_node->data.int_value;
                    }
                } else if (type_op->type == AST_IDENTIFIER && strcmp(type_op->data.string_value, "*") == 0) {
                    symbol.type = SYM_POINTER;
                } else {
                    symbol.type = SYM_INT;
                }
            } else {
                symbol.type = SYM_INT;
            }
        }

        if (stmt->data.list.count == 3) symbol.init_value = stmt->data.list.children[2];
        else if (stmt->data.list.count >= 4) symbol.init_value = stmt->data.list.children[3];

        if (symbol.init_value && symbol.init_value->type == AST_FN) {
            symbol.type = SYM_FUNCTION;
            symbol.type_info.function.param_count = symbol.init_value->data.fn.param_count;
            symbol.type_info.function.return_type = SYM_INT;
        }

        add_symbol(table, symbol);
        return;
    }

    if (strcmp(op->data.string_value, "begin") == 0) {
        SymbolTable *child = create_child_scope(table);
        stmt->scope = child;
        for (size_t i = 1; i < stmt->data.list.count; i++) collect_symbols_from_stmt(stmt->data.list.children[i], child);
        return;
    }

    if (strcmp(op->data.string_value, "if") == 0) {
        if (stmt->data.list.count >= 3) {
            ASTNode *then_branch = stmt->data.list.children[2];
            if (then_branch->type == AST_LIST && then_branch->data.list.count >= 1) {
                ASTNode *then_op = then_branch->data.list.children[0];
                if (then_op->type == AST_IDENTIFIER && strcmp(then_op->data.string_value, "begin") == 0) {
                    SymbolTable *child = create_child_scope(table);
                    then_branch->scope = child;
                    collect_symbols_from_block(then_branch, child);
                } else {
                    collect_symbols_from_stmt(then_branch, table);
                }
            }
        }
        if (stmt->data.list.count >= 4) {
            ASTNode *else_branch = stmt->data.list.children[3];
            if (else_branch->type == AST_LIST && else_branch->data.list.count >= 1) {
                ASTNode *else_op = else_branch->data.list.children[0];
                if (else_op->type == AST_IDENTIFIER && strcmp(else_op->data.string_value, "begin") == 0) {
                    SymbolTable *child = create_child_scope(table);
                    else_branch->scope = child;
                    collect_symbols_from_block(else_branch, child);
                } else {
                    collect_symbols_from_stmt(else_branch, table);
                }
            }
        }
        return;
    }

    if (strcmp(op->data.string_value, "while") == 0) {
        if (stmt->data.list.count >= 3) {
            ASTNode *body = stmt->data.list.children[2];
            if (body->type == AST_LIST && body->data.list.count >= 1) {
                ASTNode *body_op = body->data.list.children[0];
                if (body_op->type == AST_IDENTIFIER && strcmp(body_op->data.string_value, "begin") == 0) {
                    SymbolTable *child = create_child_scope(table);
                    body->scope = child;
                    collect_symbols_from_block(body, child);
                } else {
                    collect_symbols_from_stmt(body, table);
                }
            }
        }
    }
}

SymbolTable *build_symbol_table(ASTNode *ast) {
    SymbolTable *table = arena_calloc(&g_arena_compile, 1, sizeof(SymbolTable));
    if (ast->type == AST_LIST) {
        for (size_t i = 0; i < ast->data.list.count; i++) {
            collect_symbols_from_stmt(ast->data.list.children[i], table);
        }
    }
    return table;
}

bool ast_uses_exponentiation(ASTNode *node) {
    if (!node) return false;
    if (node->type == AST_FN) return ast_uses_exponentiation(node->data.fn.body);
    if (node->type == AST_LIST && node->data.list.count > 0) {
        ASTNode *first = node->data.list.children[0];
        if (first->type == AST_IDENTIFIER && strcmp(first->data.string_value, "**") == 0) return true;
        for (size_t i = 0; i < node->data.list.count; i++) {
            if (ast_uses_exponentiation(node->data.list.children[i])) return true;
        }
    }
    return false;
}
