#include "compiler.h"
#include <stdarg.h>

// Global struct type table  
static StructTypeTable *global_struct_types = NULL;

CodeGen *create_codegen(void) {
    CodeGen *codegen = malloc(sizeof(CodeGen));
    if (!codegen) {
        fprintf(stderr, "Error: Failed to allocate memory for code generator\n");
        exit(1);
    }
    
    codegen->output_capacity = 4096;
    codegen->output = malloc(codegen->output_capacity);
    if (!codegen->output) {
        fprintf(stderr, "Error: Failed to allocate memory for output buffer\n");
        exit(1);
    }
    
    codegen->output[0] = '\0';
    codegen->output_size = 0;
    codegen->label_counter = 0;
    
    return codegen;
}

void free_codegen(CodeGen *codegen) {
    if (codegen) {
        free(codegen->output);
        free(codegen);
    }
}

void emit_code(CodeGen *codegen, const char *format, ...) {
    va_list args;
    va_start(args, format);
    
    // Calculate needed size
    va_list args_copy;
    va_copy(args_copy, args);
    int needed = vsnprintf(NULL, 0, format, args_copy);
    va_end(args_copy);
    
    // Ensure buffer is large enough
    while (codegen->output_size + needed + 1 >= codegen->output_capacity) {
        codegen->output_capacity *= 2;
        codegen->output = realloc(codegen->output, codegen->output_capacity);
        if (!codegen->output) {
            fprintf(stderr, "Error: Failed to reallocate output buffer\n");
            exit(1);
        }
    }
    
    // Append to buffer
    int written = vsnprintf(codegen->output + codegen->output_size, 
                           codegen->output_capacity - codegen->output_size, 
                           format, args);
    codegen->output_size += written;
    
    va_end(args);
}

char *new_label(CodeGen *codegen, const char *prefix) {
    codegen->label_counter++;
    char *label = malloc(strlen(prefix) + 20);
    sprintf(label, ".%s_%d", prefix, codegen->label_counter);
    return label;
}

SymbolTable *build_symbol_table(ASTNode *ast) {
    SymbolTable *table = malloc(sizeof(SymbolTable));
    if (!table) {
        fprintf(stderr, "Error: Failed to allocate memory for symbol table\n");
        exit(1);
    }
    
    table->symbols = NULL;
    table->count = 0;
    table->capacity = 0;
    table->parent = NULL;  // Global symbol table has no parent
    
    // Traverse AST and collect symbols
    if (ast->type == AST_LIST) {
        for (size_t i = 0; i < ast->data.list.count; i++) {
            ASTNode *stmt = ast->data.list.children[i];
            
            if (stmt->type == AST_LIST && stmt->data.list.count >= 3) {
                ASTNode *op = stmt->data.list.children[0];
                
                if (op->type == AST_IDENTIFIER && strcmp(op->data.string_value, "let") == 0) {
                    // Variable declaration
                    ASTNode *name_node = stmt->data.list.children[1];
                    
                    if (name_node->type == AST_IDENTIFIER) {
                        Symbol symbol;
                        symbol.name = strdup(name_node->data.string_value);
                        
                        // Determine if this is 3-element (let name value) or 4-element (let name type value) format
                        if (stmt->data.list.count == 3) {
                            // 3-element format: (let name value) - infer type from value
                            symbol.type = SYM_INT; // default type for 3-element format
                        } else if (stmt->data.list.count >= 4) {
                            // 4-element format: (let name type value) - explicit type
                            ASTNode *type_node = stmt->data.list.children[2];
                            
                            // Parse type
                            if (type_node->type == AST_IDENTIFIER) {
                                if (strcmp(type_node->data.string_value, "int") == 0) {
                                    symbol.type = SYM_INT;
                                } else if (strcmp(type_node->data.string_value, "str") == 0) {
                                    symbol.type = SYM_STR;
                                } else if (strcmp(type_node->data.string_value, "char") == 0) {
                                    symbol.type = SYM_CHAR;
                                } else if (strcmp(type_node->data.string_value, "bool") == 0) {
                                    symbol.type = SYM_BOOL;
                                } else if (strcmp(type_node->data.string_value, "struct") == 0) {
                                    // Skip struct type definitions, they don't create variables
                                    continue;
                                } else {
                                    // This might be a user-defined struct type name
                                    symbol.type = SYM_STRUCT;
                                    symbol.type_info.struct_instance.struct_type_name = strdup(type_node->data.string_value);
                                }
                            } else if (type_node->type == AST_LIST && type_node->data.list.count >= 2) {
                                // Handle array types: (array_type int 4) or ([] int 4)
                                ASTNode *array_op = type_node->data.list.children[0];
                                if (array_op->type == AST_IDENTIFIER && 
                                    (strcmp(array_op->data.string_value, "array_type") == 0 ||
                                     strcmp(array_op->data.string_value, "[]") == 0)) {
                                    symbol.type = SYM_ARRAY; // Properly set as array type
                                } else {
                                    symbol.type = SYM_INT; // default
                                }
                            } else {
                                symbol.type = SYM_INT; // default
                            }
                        } else {
                            symbol.type = SYM_INT; // default
                        }
                        
                        // Check different let statement formats:
                        // 3 elements: (let name (fn ...))  
                        // 4 elements: (let name type value) or (let name type (fn ...))
                        if (stmt->data.list.count == 3) {
                            symbol.init_value = stmt->data.list.children[2];
                        } else if (stmt->data.list.count >= 4) {
                            symbol.init_value = stmt->data.list.children[3];
                        } else {
                            symbol.init_value = NULL;
                        }
                        
                        // Check if this is a function definition: (let name (fn ...))
                        if (symbol.init_value && symbol.init_value->type == AST_LIST && 
                            symbol.init_value->data.list.count >= 3) {
                            ASTNode *fn_node = symbol.init_value->data.list.children[0];
                            if (fn_node->type == AST_IDENTIFIER && strcmp(fn_node->data.string_value, "fn") == 0) {
                                symbol.type = SYM_FUNCTION;
                                // For now, set basic function info (can be expanded later)
                                symbol.type_info.function.param_count = 0;
                                symbol.type_info.function.param_types = NULL;
                                symbol.type_info.function.return_type = SYM_INT; // default
                            }
                        }
                        
                        add_symbol(table, symbol);
                    }
                }
            }
        }
    }
    
    return table;
}

void add_symbol(SymbolTable *table, Symbol symbol) {
    if (table->count >= table->capacity) {
        table->capacity = table->capacity == 0 ? 16 : table->capacity * 2;
        table->symbols = realloc(table->symbols, table->capacity * sizeof(Symbol));
        if (!table->symbols) {
            fprintf(stderr, "Error: Failed to allocate memory for symbols\n");
            exit(1);
        }
    }
    table->symbols[table->count++] = symbol;
}

Symbol *find_symbol(SymbolTable *table, const char *name) {
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->symbols[i].name, name) == 0) {
            return &table->symbols[i];
        }
    }
    return NULL;
}

int get_symbol_offset(SymbolTable *table, const char *name) {
    int offset = 0;
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->symbols[i].name, name) == 0) {
            return offset;
        }
        // Calculate space needed for this symbol
        if (table->symbols[i].type == SYM_ARRAY) {
            offset += 32; // 4 elements × 8 bytes for arrays (assuming int[4])
        } else if (table->symbols[i].type == SYM_STRUCT) {
            offset += 16; // 2 fields × 8 bytes for structs  
        } else {
            offset += 8; // 8 bytes for regular variables
        }
    }
    return -1; // Not found
}

void free_symbol_table(SymbolTable *table) {
    if (table) {
        for (size_t i = 0; i < table->count; i++) {
            free(table->symbols[i].name);
        }
        free(table->symbols);
        free(table);
    }
}

Symbol *find_symbol_recursive(SymbolTable *table, const char *name) {
    if (!table) return NULL;
    
    // Search current scope
    Symbol *symbol = find_symbol(table, name);
    if (symbol) return symbol;
    
    // Search parent scope
    if (table->parent) {
        return find_symbol_recursive(table->parent, name);
    }
    
    return NULL;
}

bool uses_exponentiation(ASTNode *node) {
    if (!node) return false;
    
    if (node->type == AST_LIST && node->data.list.count > 0) {
        // Check if this is an exponentiation operation
        ASTNode *first = node->data.list.children[0];
        if (first->type == AST_IDENTIFIER && strcmp(first->data.string_value, "**") == 0) {
            return true;
        }
        
        // Recursively check children
        for (size_t i = 0; i < node->data.list.count; i++) {
            if (uses_exponentiation(node->data.list.children[i])) {
                return true;
            }
        }
    }
    
    return false;
}

void generate_pow_function(CodeGen *codegen) {
    emit_code(codegen,
        "\n"
        "// Power function: x0 = x0 ^ x1\n"
        "pow:\n"
        "    // Save registers\n"
        "    stp x2, x3, [sp, #-16]!\n"
        "    stp x4, x30, [sp, #-16]!\n"
        "    \n"
        "    // Handle special cases\n"
        "    mov x2, x0           // x2 = base\n"
        "    mov x3, x1           // x3 = exponent\n"
        "    mov x0, #1           // result = 1\n"
        "    \n"
        "    // If exponent is 0, return 1\n"
        "    cbz x3, pow_done\n"
        "    \n"
        "    // Handle negative exponent (return 0 for negative exponents in integer math)\n"
        "    tbnz x3, #63, pow_zero\n"
        "    \n"
        "    // Main exponentiation loop\n"
        "pow_loop:\n"
        "    // If exponent is 0, we're done\n"
        "    cbz x3, pow_done\n"
        "    \n"
        "    // Check if exponent is odd\n"
        "    tbnz x3, #0, pow_odd\n"
        "    \n"
        "    // Exponent is even: square the base, halve the exponent\n"
        "    mul x2, x2, x2\n"
        "    lsr x3, x3, #1\n"
        "    b pow_loop\n"
        "    \n"
        "pow_odd:\n"
        "    // Multiply result by current base\n"
        "    mul x0, x0, x2\n"
        "    // Decrement exponent by 1\n"
        "    sub x3, x3, #1\n"
        "    // Continue with the loop\n"
        "    b pow_loop\n"
        "    \n"
        "pow_zero:\n"
        "    mov x0, #0\n"
        "    \n"
        "pow_done:\n"
        "    // Restore registers and return\n"
        "    ldp x4, x30, [sp], #16\n"
        "    ldp x2, x3, [sp], #16\n"
        "    ret\n"
        "\n");
}

void generate_preamble(CodeGen *codegen) {
    emit_code(codegen,
        "    .text\n"
        "    .globl  _main\n"
        "    .p2align    2\n");
}

void emit_mov_immediate(CodeGen *codegen, const char *reg, int value) {
    emit_code(codegen, "    mov   %s, #%d\n", reg, value);
}

void emit_load_variable(CodeGen *codegen, const char *reg, const char *var_name, SymbolTable *symbols) {
    emit_load_variable_with_adjustment(codegen, reg, var_name, symbols, 0);
}

void emit_load_variable_with_adjustment(CodeGen *codegen, const char *reg, const char *var_name, SymbolTable *symbols, int stack_adjustment) {
    int offset = get_symbol_offset(symbols, var_name);
    if (offset >= 0) {
        emit_code(codegen, "    ldr   %s, [sp, #%d]\n", reg, offset + stack_adjustment);
    } else {
        emit_code(codegen, "    mov   %s, #0  // Error: undefined variable %s\n", reg, var_name);
    }
}

void emit_store_variable(CodeGen *codegen, const char *reg, const char *var_name, SymbolTable *symbols) {
    int offset = get_symbol_offset(symbols, var_name);
    if (offset >= 0) {
        emit_code(codegen, "    str   %s, [sp, #%d]\n", reg, offset);
    } else {
        emit_code(codegen, "    // Error: undefined variable %s\n", var_name);
    }
}

void emit_arithmetic(CodeGen *codegen, const char *op, const char *dest, const char *src1, const char *src2) {
    if (strcmp(op, "+") == 0) {
        emit_code(codegen, "    add   %s, %s, %s\n", dest, src1, src2);
    } else if (strcmp(op, "-") == 0) {
        emit_code(codegen, "    sub   %s, %s, %s\n", dest, src1, src2);
    } else if (strcmp(op, "*") == 0) {
        emit_code(codegen, "    mul   %s, %s, %s\n", dest, src1, src2);
    } else if (strcmp(op, "/") == 0) {
        emit_code(codegen, "    sdiv  %s, %s, %s\n", dest, src1, src2);
    } else if (strcmp(op, "%") == 0) {
        emit_code(codegen, "    sdiv  x4, %s, %s\n", src1, src2);
        emit_code(codegen, "    msub  %s, x4, %s, %s\n", dest, src2, src1);
    } else if (strcmp(op, "**") == 0) {
        emit_code(codegen, "    mov   x0, %s\n", src1);
        emit_code(codegen, "    mov   x1, %s\n", src2);
        emit_code(codegen, "    bl    pow\n");
        emit_code(codegen, "    mov   %s, x0\n", dest);
    } else if (strcmp(op, "==") == 0) {
        emit_code(codegen, "    cmp   %s, %s\n", src1, src2);
        emit_code(codegen, "    cset  %s, eq\n", dest);
    } else if (strcmp(op, "<") == 0) {
        emit_code(codegen, "    cmp   %s, %s\n", src1, src2);
        emit_code(codegen, "    cset  %s, lt\n", dest);
    } else if (strcmp(op, ">") == 0) {
        emit_code(codegen, "    cmp   %s, %s\n", src1, src2);
        emit_code(codegen, "    cset  %s, gt\n", dest);
    } else if (strcmp(op, "<=") == 0) {
        emit_code(codegen, "    cmp   %s, %s\n", src1, src2);
        emit_code(codegen, "    cset  %s, le\n", dest);
    } else if (strcmp(op, ">=") == 0) {
        emit_code(codegen, "    cmp   %s, %s\n", src1, src2);
        emit_code(codegen, "    cset  %s, ge\n", dest);
    }
}

void generate_expression_with_adjustment(CodeGen *codegen, ASTNode *expr, SymbolTable *symbols, int stack_adjustment) {
    if (!expr) return;
    
    switch (expr->type) {
        case AST_INT:
            emit_mov_immediate(codegen, "x0", expr->data.int_value);
            break;
            
        case AST_CHAR:
            emit_mov_immediate(codegen, "x0", (int)expr->data.char_value);
            break;
            
        case AST_IDENTIFIER:
            emit_load_variable_with_adjustment(codegen, "x0", expr->data.string_value, symbols, stack_adjustment);
            break;
            
        case AST_LIST:
            if (expr->data.list.count > 0) {
                ASTNode *op = expr->data.list.children[0];
                
                if (op->type == AST_IDENTIFIER) {
                    // Handle arithmetic and comparison operators
                    if (strcmp(op->data.string_value, "+") == 0 ||
                        strcmp(op->data.string_value, "-") == 0 ||
                        strcmp(op->data.string_value, "*") == 0 ||
                        strcmp(op->data.string_value, "/") == 0 ||
                        strcmp(op->data.string_value, "%") == 0 ||
                        strcmp(op->data.string_value, "**") == 0 ||
                        strcmp(op->data.string_value, "==") == 0 ||
                        strcmp(op->data.string_value, "<") == 0 ||
                        strcmp(op->data.string_value, ">") == 0 ||
                        strcmp(op->data.string_value, "<=") == 0 ||
                        strcmp(op->data.string_value, ">=") == 0) {
                        
                        if (expr->data.list.count >= 3) {
                            // Generate code for operands
                            ASTNode *left = expr->data.list.children[1];
                            ASTNode *right = expr->data.list.children[2];
                            
                            // Load left operand into x2
                            if (left->type == AST_INT) {
                                emit_mov_immediate(codegen, "x2", left->data.int_value);
                            } else if (left->type == AST_IDENTIFIER) {
                                emit_load_variable_with_adjustment(codegen, "x2", left->data.string_value, symbols, stack_adjustment);
                            } else if (left->type == AST_LIST) {
                                generate_expression_with_adjustment(codegen, left, symbols, stack_adjustment);
                                emit_code(codegen, "    mov   x2, x0\n");
                            }
                            
                            // Load right operand into x3
                            if (right->type == AST_INT) {
                                emit_mov_immediate(codegen, "x3", right->data.int_value);
                            } else if (right->type == AST_IDENTIFIER) {
                                emit_load_variable_with_adjustment(codegen, "x3", right->data.string_value, symbols, stack_adjustment);
                            } else if (right->type == AST_LIST) {
                                generate_expression_with_adjustment(codegen, right, symbols, stack_adjustment);
                                emit_code(codegen, "    mov   x3, x0\n");
                            }
                            
                            // Perform operation
                            emit_arithmetic(codegen, op->data.string_value, "x0", "x2", "x3");
                        }
                    }
                    // For other operations, fall back to normal expression generation
                    else {
                        generate_expression(codegen, expr, symbols);
                    }
                }
            }
            break;
            
        default:
            emit_mov_immediate(codegen, "x0", 0);
            break;
    }
}

void generate_expression(CodeGen *codegen, ASTNode *expr, SymbolTable *symbols) {
    if (!expr) return;
    
    switch (expr->type) {
        case AST_INT:
            emit_mov_immediate(codegen, "x0", expr->data.int_value);
            break;
            
        case AST_CHAR:
            emit_mov_immediate(codegen, "x0", (int)expr->data.char_value);
            break;
            
        case AST_IDENTIFIER:
            emit_load_variable(codegen, "x0", expr->data.string_value, symbols);
            break;
            
        case AST_LIST:
            if (expr->data.list.count > 0) {
                ASTNode *op = expr->data.list.children[0];
                
                if (op->type == AST_IDENTIFIER) {
                    // Handle arithmetic and comparison operators
                    if (strcmp(op->data.string_value, "+") == 0 ||
                        strcmp(op->data.string_value, "-") == 0 ||
                        strcmp(op->data.string_value, "*") == 0 ||
                        strcmp(op->data.string_value, "/") == 0 ||
                        strcmp(op->data.string_value, "%") == 0 ||
                        strcmp(op->data.string_value, "**") == 0 ||
                        strcmp(op->data.string_value, "==") == 0 ||
                        strcmp(op->data.string_value, "<") == 0 ||
                        strcmp(op->data.string_value, ">") == 0 ||
                        strcmp(op->data.string_value, "<=") == 0 ||
                        strcmp(op->data.string_value, ">=") == 0) {
                        
                        if (expr->data.list.count >= 3) {
                            // Generate code for operands
                            ASTNode *left = expr->data.list.children[1];
                            ASTNode *right = expr->data.list.children[2];
                            
                            // Check if right operand contains function calls that might corrupt x2
                            bool right_has_function_call = false;
                            if (right->type == AST_LIST) {
                                // This is a simplification - assume any complex expression might have function calls
                                right_has_function_call = true;
                            }
                            
                            if (right_has_function_call && left->type == AST_IDENTIFIER) {
                                // Load right operand first to avoid register corruption
                                generate_expression(codegen, right, symbols);
                                emit_code(codegen, "    mov   x3, x0\n");
                                
                                // Then load left operand into x2
                                emit_load_variable(codegen, "x2", left->data.string_value, symbols);
                            } else {
                                // Standard order: load left, then right
                                // Load left operand into x2
                                if (left->type == AST_INT) {
                                    emit_mov_immediate(codegen, "x2", left->data.int_value);
                                } else if (left->type == AST_IDENTIFIER) {
                                    emit_load_variable(codegen, "x2", left->data.string_value, symbols);
                                } else if (left->type == AST_LIST) {
                                    generate_expression(codegen, left, symbols);
                                    emit_code(codegen, "    mov   x2, x0\n");
                                }
                                
                                // Load right operand into x3
                                if (right->type == AST_INT) {
                                    emit_mov_immediate(codegen, "x3", right->data.int_value);
                                } else if (right->type == AST_IDENTIFIER) {
                                    emit_load_variable(codegen, "x3", right->data.string_value, symbols);
                                } else if (right->type == AST_LIST) {
                                    generate_expression(codegen, right, symbols);
                                    emit_code(codegen, "    mov   x3, x0\n");
                                }
                            }
                            
                            // Perform operation
                            emit_arithmetic(codegen, op->data.string_value, "x0", "x2", "x3");
                        }
                    }
                    // Handle array access: ([] array_var index)
                    else if (strcmp(op->data.string_value, "[]") == 0) {
                        if (expr->data.list.count >= 3) {
                            ASTNode *array_var = expr->data.list.children[1];
                            ASTNode *index = expr->data.list.children[2];
                            
                            if (array_var->type == AST_IDENTIFIER) {
                                // Get the base offset of the array
                                int array_offset = get_symbol_offset(symbols, array_var->data.string_value);
                                
                                if (array_offset >= 0) {
                                    // Calculate element offset based on index
                                    int element_offset = 0;
                                    if (index->type == AST_INT) {
                                        // Static index - calculate offset directly
                                        element_offset = index->data.int_value * 8;  // 8 bytes per element
                                        emit_code(codegen, "    ldr   x0, [sp, #%d]\n", array_offset + element_offset);
                                    } else {
                                        // Dynamic index - need to calculate at runtime
                                        // Load index into x1
                                        if (index->type == AST_IDENTIFIER) {
                                            emit_load_variable(codegen, "x1", index->data.string_value, symbols);
                                        } else {
                                            generate_expression(codegen, index, symbols);
                                            emit_code(codegen, "    mov   x1, x0\n");
                                        }
                                        
                                        // Calculate element address: base + (index * 8)
                                        emit_code(codegen, "    mov   x2, #8\n");
                                        emit_code(codegen, "    mul   x1, x1, x2\n");
                                        emit_code(codegen, "    mov   x2, sp\n");
                                        emit_code(codegen, "    add   x2, x2, #%d\n", array_offset);
                                        emit_code(codegen, "    add   x2, x2, x1\n");
                                        emit_code(codegen, "    ldr   x0, [x2]\n");
                                    }
                                } else {
                                    emit_mov_immediate(codegen, "x0", 0);
                                }
                            }
                        }
                    }
                    // Handle field access: (. struct_var field_name)
                    else if (strcmp(op->data.string_value, ".") == 0) {
                        if (expr->data.list.count >= 3) {
                            ASTNode *struct_var = expr->data.list.children[1];
                            ASTNode *field_name = expr->data.list.children[2];
                            
                            if (struct_var->type == AST_IDENTIFIER && field_name->type == AST_IDENTIFIER) {
                                // Find the struct variable to get its type
                                Symbol *struct_symbol = find_symbol(symbols, struct_var->data.string_value);
                                if (struct_symbol && struct_symbol->type == SYM_STRUCT) {
                                    // Get the struct type name from the symbol
                                    const char *struct_type_name = struct_symbol->type_info.struct_instance.struct_type_name;
                                    
                                    // Find the struct type definition
                                    StructType *struct_type = find_struct_type(global_struct_types, struct_type_name);
                                    if (struct_type) {
                                        // Find the field in the struct type definition
                                        const char *field = field_name->data.string_value;
                                        int field_offset = -1;
                                        
                                        for (size_t i = 0; i < struct_type->field_count; i++) {
                                            if (strcmp(struct_type->fields[i].name, field) == 0) {
                                                field_offset = i * 8; // Each field is 8 bytes apart
                                                break;
                                            }
                                        }
                                        
                                        if (field_offset >= 0) {
                                            // Load struct base address
                                            int struct_offset = get_symbol_offset(symbols, struct_var->data.string_value);
                                            if (struct_offset >= 0) {
                                                // Load field value at struct_base + field_offset
                                                emit_code(codegen, "    ldr   x0, [sp, #%d]\n", struct_offset + field_offset);
                                            } else {
                                                emit_mov_immediate(codegen, "x0", 0);
                                            }
                                        } else {
                                            // Field not found, emit error value
                                            emit_mov_immediate(codegen, "x0", 0);
                                        }
                                    } else {
                                        // Struct type not found, emit error value
                                        emit_mov_immediate(codegen, "x0", 0);
                                    }
                                } else {
                                    // Variable is not a struct, emit error value
                                    emit_mov_immediate(codegen, "x0", 0);
                                }
                            }
                        }
                    }
                    // Handle struct literals: (# ((field1 value1) (field2 value2) ...))
                    else if (strcmp(op->data.string_value, "#") == 0) {
                        if (expr->data.list.count >= 2) {
                            ASTNode *fields = expr->data.list.children[1];
                            
                            // For struct initialization, just return the first field's value
                            // The actual struct layout will be handled during variable assignment
                            if (fields->type == AST_LIST && fields->data.list.count > 0) {
                                ASTNode *first_field = fields->data.list.children[0];
                                if (first_field->type == AST_LIST && first_field->data.list.count >= 2) {
                                    ASTNode *field_value = first_field->data.list.children[1];
                                    generate_expression(codegen, field_value, symbols);
                                } else {
                                    emit_mov_immediate(codegen, "x0", 0);
                                }
                            } else {
                                emit_mov_immediate(codegen, "x0", 0);
                            }
                        } else {
                            emit_mov_immediate(codegen, "x0", 0);
                        }
                    }
                    // Handle function calls: (function_name arg1 arg2 ...)
                    else {
                        // Check if this is a function call using recursive symbol lookup
                        Symbol *func_symbol = find_symbol_recursive(symbols, op->data.string_value);
                        if (func_symbol && func_symbol->type == SYM_FUNCTION) {
                            // Generate function call
                            emit_code(codegen, "    // Function call: %s\n", op->data.string_value);
                            
                            // Save link register
                            emit_code(codegen, "    stp   x29, x30, [sp, #-16]!\n");
                            
                            // Pass arguments - for structs, we need to copy all fields
                            int arg_count = expr->data.list.count - 1; // Subtract 1 for function name
                            int reg_index = 0;
                            
                            for (int i = 0; i < arg_count && reg_index < 4; i++) {
                                ASTNode *arg = expr->data.list.children[i + 1];
                                if (arg->type == AST_INT) {
                                    emit_mov_immediate(codegen, "x9", arg->data.int_value);
                                    emit_code(codegen, "    mov   x%d, x9\n", reg_index);
                                    reg_index++;
                                } else if (arg->type == AST_IDENTIFIER) {
                                    // Check if this is a struct or array variable by looking at symbol table
                                    Symbol *arg_symbol = find_symbol(symbols, arg->data.string_value);
                                    if (arg_symbol && arg_symbol->type == SYM_STRUCT) {
                                        // This is a struct - copy all fields (assuming 2 fields for now)
                                        int struct_offset = get_symbol_offset(symbols, arg->data.string_value);
                                        // Copy field 0 (x)
                                        emit_code(codegen, "    ldr   x9, [sp, #%d]\n", struct_offset + 16);
                                        emit_code(codegen, "    mov   x%d, x9\n", reg_index);
                                        reg_index++;
                                        // Copy field 1 (y) if we have register space
                                        if (reg_index < 4) {
                                            emit_code(codegen, "    ldr   x9, [sp, #%d]\n", struct_offset + 8 + 16);
                                            emit_code(codegen, "    mov   x%d, x9\n", reg_index);
                                            reg_index++;
                                        }
                                    } else if (arg_symbol && arg_symbol->type == SYM_ARRAY) {
                                        // This is an array - copy all elements (assuming 4 elements for now)
                                        int array_offset = get_symbol_offset(symbols, arg->data.string_value);
                                        // Copy elements 0, 1, 2, 3
                                        for (int elem = 0; elem < 4 && reg_index < 4; elem++) {
                                            emit_code(codegen, "    ldr   x9, [sp, #%d]\n", array_offset + elem * 8 + 16);
                                            emit_code(codegen, "    mov   x%d, x9\n", reg_index);
                                            reg_index++;
                                        }
                                    } else {
                                        // Regular variable - need to account for stack pointer movement due to stp instruction
                                        int arg_offset = get_symbol_offset(symbols, arg->data.string_value);
                                        if (arg_offset >= 0) {
                                            // Add 16 to offset to account for saved registers
                                            emit_code(codegen, "    ldr   x9, [sp, #%d]\n", arg_offset + 16);
                                        } else {
                                            emit_mov_immediate(codegen, "x9", 0);
                                        }
                                        emit_code(codegen, "    mov   x%d, x9\n", reg_index);
                                        reg_index++;
                                    }
                                } else if (arg->type == AST_LIST) {
                                    // For complex expressions inside function calls, we need to account for stack adjustment
                                    // The stack pointer has moved due to the stp instruction above
                                    generate_expression_with_adjustment(codegen, arg, symbols, 16);
                                    emit_code(codegen, "    mov   x%d, x0\n", reg_index);
                                    reg_index++;
                                }
                            }
                            
                            emit_code(codegen, "    bl    %s\n", op->data.string_value);
                            
                            // Restore link register
                            emit_code(codegen, "    ldp   x29, x30, [sp], #16\n");
                        } else {
                            // Unknown function or identifier, return 0
                            emit_mov_immediate(codegen, "x0", 0);
                        }
                    }
                }
            }
            break;
            
        case AST_ARRAY:
            // Handle array literals [1 2 3 4]
            // For now, just load the first element (simplified implementation)
            if (expr->data.list.count > 0) {
                generate_expression(codegen, expr->data.list.children[0], symbols);
            } else {
                emit_mov_immediate(codegen, "x0", 0);
            }
            break;
            
        default:
            emit_mov_immediate(codegen, "x0", 0);
            break;
    }
}

void generate_statement(CodeGen *codegen, ASTNode *stmt, SymbolTable *symbols) {
    if (!stmt || stmt->type != AST_LIST || stmt->data.list.count == 0) {
        return;
    }
    
    ASTNode *op = stmt->data.list.children[0];
    if (op->type != AST_IDENTIFIER) {
        return;
    }
    
    if (strcmp(op->data.string_value, "let") == 0) {
        // Variable declaration: (let name type [init]) or (let name value)
        if (stmt->data.list.count >= 3) {
            ASTNode *name_node = stmt->data.list.children[1];
            
            if (stmt->data.list.count >= 4) {
                // 4-element format: (let name type init)
                ASTNode *type_node = stmt->data.list.children[2];
                ASTNode *init = stmt->data.list.children[3];
                
                // Check if this is a struct type definition: (let TypeName struct #((field1 type1 val1) ...))
                if (type_node->type == AST_IDENTIFIER && strcmp(type_node->data.string_value, "struct") == 0) {
                    // This is a struct type definition - parse and store it
                    if (init->type == AST_LIST && init->data.list.count >= 2 &&
                        init->data.list.children[0]->type == AST_IDENTIFIER &&
                        strcmp(init->data.list.children[0]->data.string_value, "#") == 0) {
                        
                        // Parse struct fields from #((field1 type1 val1) (field2 type2 val2) ...)
                        ASTNode *fields_list = init->data.list.children[1];
                        if (fields_list->type == AST_LIST) {
                            // Create struct fields array
                            size_t field_count = fields_list->data.list.count;
                            StructField *fields = malloc(field_count * sizeof(StructField));
                            
                            for (size_t i = 0; i < field_count; i++) {
                                ASTNode *field = fields_list->data.list.children[i];
                                if (field->type == AST_LIST && field->data.list.count >= 3) {
                                    ASTNode *field_name = field->data.list.children[0];
                                    ASTNode *field_type = field->data.list.children[1];
                                    ASTNode *field_default = field->data.list.children[2];
                                    
                                    if (field_name->type == AST_IDENTIFIER && field_type->type == AST_IDENTIFIER) {
                                        fields[i].name = strdup(field_name->data.string_value);
                                        
                                        // Map type string to SymbolType
                                        const char *type_str = field_type->data.string_value;
                                        if (strcmp(type_str, "int") == 0) {
                                            fields[i].type = SYM_INT;
                                        } else if (strcmp(type_str, "char") == 0) {
                                            fields[i].type = SYM_CHAR;
                                        } else if (strcmp(type_str, "str") == 0) {
                                            fields[i].type = SYM_STR;
                                        } else if (strcmp(type_str, "bool") == 0) {
                                            fields[i].type = SYM_BOOL;
                                        } else {
                                            // Assume it's a user-defined struct type
                                            fields[i].type = SYM_STRUCT;
                                        }
                                        
                                        fields[i].default_value = field_default; // Store reference to default value
                                    }
                                }
                            }
                            
                            // Initialize global struct types table if not already done
                            if (!global_struct_types) {
                                global_struct_types = create_struct_type_table();
                            }
                            
                            // Add struct type to table
                            add_struct_type(global_struct_types, name_node->data.string_value, fields, field_count);
                            
                            // Clean up temporary fields array (add_struct_type makes its own copy)
                            for (size_t i = 0; i < field_count; i++) {
                                free(fields[i].name);
                            }
                            free(fields);
                        }
                    }
                    return; // Don't generate code for struct type definitions
                }
                
                // Check if this is a struct or array initialization
                if (init->type == AST_ARRAY) {
                    // Handle AST_ARRAY type: [1 2 3 4]
                    int base_offset = get_symbol_offset(symbols, name_node->data.string_value);
                    for (size_t i = 0; i < init->data.list.count; i++) {
                        ASTNode *element = init->data.list.children[i];
                        generate_expression(codegen, element, symbols);
                        // Store each element at base + element_index * 8
                        emit_code(codegen, "    str   x0, [sp, #%d]\n", base_offset + (int)i * 8);
                    }
                } else if (init->type == AST_LIST && init->data.list.count >= 2) {
                    ASTNode *init_op = init->data.list.children[0];
                    if (init_op->type == AST_IDENTIFIER && strcmp(init_op->data.string_value, "#") == 0) {
                        // Check if we have a type to determine if this is array or struct
                        bool is_array_type = false;
                        bool is_struct_type = false;
                        
                        if (type_node->type == AST_IDENTIFIER && strstr(type_node->data.string_value, "[") != NULL) {
                            is_array_type = true;
                        } else if (type_node->type == AST_LIST && type_node->data.list.count >= 1) {
                            ASTNode *first_child = type_node->data.list.children[0];
                            if (first_child->type == AST_IDENTIFIER && strcmp(first_child->data.string_value, "[]") == 0) {
                                is_array_type = true;
                            }
                        } else if (type_node->type == AST_IDENTIFIER) {
                            // Check if this is a user-defined struct type by looking in the struct type table
                            const char *type_name = type_node->data.string_value;
                            if (global_struct_types && find_struct_type(global_struct_types, type_name)) {
                                is_struct_type = true;
                            }
                        }
                        if (is_array_type) {
                            // This is an array literal like #(10 20 30 40)
                            ASTNode *elements = init->data.list.children[1];
                            if (elements->type == AST_LIST) {
                                int base_offset = get_symbol_offset(symbols, name_node->data.string_value);
                                for (size_t i = 0; i < elements->data.list.count; i++) {
                                    ASTNode *element = elements->data.list.children[i];
                                    generate_expression(codegen, element, symbols);
                                    // Store each element at base + element_index * 8
                                    emit_code(codegen, "    str   x0, [sp, #%d]\n", base_offset + (int)i * 8);
                                }
                            }
                        } else if (is_struct_type) {
                            // This is a user-defined struct instance with positional values: #(val1 val2 ...)
                            ASTNode *values = init->data.list.children[1];
                            if (values->type == AST_LIST) {
                                int base_offset = get_symbol_offset(symbols, name_node->data.string_value);
                                for (size_t i = 0; i < values->data.list.count; i++) {
                                    ASTNode *value = values->data.list.children[i];
                                    generate_expression(codegen, value, symbols);
                                    // Store each field at base + field_index * 8
                                    emit_code(codegen, "    str   x0, [sp, #%d]\n", base_offset + (int)i * 8);
                                }
                            }
                        } else {
                            // This is a struct literal with named fields - store each field
                            ASTNode *fields = init->data.list.children[1];
                            if (fields->type == AST_LIST) {
                                int base_offset = get_symbol_offset(symbols, name_node->data.string_value);
                                for (size_t i = 0; i < fields->data.list.count; i++) {
                                    ASTNode *field = fields->data.list.children[i];
                                    if (field->type == AST_LIST && field->data.list.count >= 2) {
                                        ASTNode *field_value = field->data.list.children[1];
                                        generate_expression(codegen, field_value, symbols);
                                        // Store each field at base + field_index * 8
                                        emit_code(codegen, "    str   x0, [sp, #%d]\n", base_offset + (int)i * 8);
                                    }
                                }
                            }
                        }
                    } else {
                        generate_expression(codegen, init, symbols);
                        emit_store_variable(codegen, "x0", name_node->data.string_value, symbols);
                    }
                } else {
                    generate_expression(codegen, init, symbols);
                    emit_store_variable(codegen, "x0", name_node->data.string_value, symbols);
                }
            } else if (stmt->data.list.count == 3) {
                // 3-element format: (let name value)
                ASTNode *init = stmt->data.list.children[2];
                
                generate_expression(codegen, init, symbols);
                emit_store_variable(codegen, "x0", name_node->data.string_value, symbols);
            } else {
                // No initializer, default to 0
                emit_mov_immediate(codegen, "x0", 0);
                emit_store_variable(codegen, "x0", name_node->data.string_value, symbols);
            }
        }
    } else if (strcmp(op->data.string_value, "set") == 0) {
        // Variable assignment: (set name value)
        if (stmt->data.list.count >= 3) {
            ASTNode *name_node = stmt->data.list.children[1];
            ASTNode *value = stmt->data.list.children[2];
            
            generate_expression(codegen, value, symbols);
            emit_store_variable(codegen, "x0", name_node->data.string_value, symbols);
        }
    } else if (strcmp(op->data.string_value, "print") == 0) {
        // Print statement: (print expr)
        if (stmt->data.list.count >= 2) {
            ASTNode *expr = stmt->data.list.children[1];
            generate_expression(codegen, expr, symbols);
            
            // Call appropriate print helper function
            // The value is already in x0 (first argument register)
            if (expr->type == AST_STRING) {
                emit_code(codegen, "    bl    _print_str\n");
            } else if (expr->type == AST_CHAR) {
                emit_code(codegen, "    bl    _print_char\n");
            } else {
                // Default to integer format
                emit_code(codegen, "    bl    _print_int\n");
            }
        }
    } else if (strcmp(op->data.string_value, "if") == 0) {
        // If statement: (if condition then-branch [else-branch])
        if (stmt->data.list.count >= 3) {
            ASTNode *condition = stmt->data.list.children[1];
            ASTNode *then_branch = stmt->data.list.children[2];
            ASTNode *else_branch = (stmt->data.list.count >= 4) ? stmt->data.list.children[3] : NULL;
            
            char *else_label = new_label(codegen, "else");
            char *end_label = new_label(codegen, "end_if");
            
            // Generate condition
            generate_expression(codegen, condition, symbols);
            emit_code(codegen, "    cbz   x0, %s\n", else_label);
            
            // Generate then branch
            generate_statement(codegen, then_branch, symbols);
            emit_code(codegen, "    b     %s\n", end_label);
            
            // Generate else branch
            emit_code(codegen, "%s:\n", else_label);
            if (else_branch) {
                generate_statement(codegen, else_branch, symbols);
            }
            
            emit_code(codegen, "%s:\n", end_label);
            
            free(else_label);
            free(end_label);
        }
    } else if (strcmp(op->data.string_value, "while") == 0) {
        // While loop: (while condition body)
        if (stmt->data.list.count >= 3) {
            ASTNode *condition = stmt->data.list.children[1];
            ASTNode *body = stmt->data.list.children[2];
            
            char *loop_label = new_label(codegen, "loop");
            char *end_label = new_label(codegen, "end_loop");
            
            // Loop start
            emit_code(codegen, "%s:\n", loop_label);
            
            // Generate condition
            generate_expression(codegen, condition, symbols);
            emit_code(codegen, "    cbz   x0, %s\n", end_label);
            
            // Generate body
            generate_statement(codegen, body, symbols);
            emit_code(codegen, "    b     %s\n", loop_label);
            
            // Loop end
            emit_code(codegen, "%s:\n", end_label);
            
            free(loop_label);
            free(end_label);
        }
    } else if (strcmp(op->data.string_value, "begin") == 0) {
        // Begin block: (begin stmt1 stmt2 ...)
        for (size_t i = 1; i < stmt->data.list.count; i++) {
            generate_statement(codegen, stmt->data.list.children[i], symbols);
        }
    } else if (strcmp(op->data.string_value, "ret") == 0) {
        // Return statement: (ret expr)
        if (stmt->data.list.count >= 2) {
            ASTNode *expr = stmt->data.list.children[1];
            generate_expression(codegen, expr, symbols);
            // Don't emit ret here - let function epilogue handle it
        } else {
            // Return void
            emit_mov_immediate(codegen, "x0", 0);
            // Don't emit ret here - let function epilogue handle it
        }
    } else {
        // Check if this is a function call
        if (stmt->type == AST_LIST && stmt->data.list.count > 0) {
            ASTNode *function_name = stmt->data.list.children[0];
            if (function_name->type == AST_IDENTIFIER) {
                Symbol *func_symbol = find_symbol(symbols, function_name->data.string_value);
                if (func_symbol && func_symbol->type == SYM_FUNCTION) {
                    // Generate function call
                    emit_code(codegen, "    // Function call: %s\n", function_name->data.string_value);
                    
                    // Save link register
                    emit_code(codegen, "    stp   x29, x30, [sp, #-16]!\n");
                    
                    // TODO: Handle function arguments properly
                    // For now, just call the function
                    emit_code(codegen, "    bl    %s\n", function_name->data.string_value);
                    
                    // Restore link register
                    emit_code(codegen, "    ldp   x29, x30, [sp], #16\n");
                    return;
                }
            }
        }
        
        // Other expressions
        generate_expression(codegen, stmt, symbols);
    }
}

void generate_function_definition(CodeGen *codegen, const char *func_name, ASTNode *fn_node, SymbolTable *symbols) {
    // Generate function label
    emit_code(codegen, "%s:\n", func_name);
    
    // Setup frame
    emit_code(codegen, "    stp   x29, x30, [sp, #-16]!\n");
    emit_code(codegen, "    mov   x29, sp\n");
    
    // Create function-local symbol table
    SymbolTable *func_symbols = malloc(sizeof(SymbolTable));
    func_symbols->symbols = NULL;
    func_symbols->count = 0;
    func_symbols->capacity = 0;
    func_symbols->parent = symbols;  // Link to global symbol table
    
    // Extract parameters and create local symbols
    int param_count = 0;
    if (fn_node->data.list.count >= 2) {
        ASTNode *params = fn_node->data.list.children[1];
        if (params->type == AST_LIST || params->type == AST_ARRAY) {
            param_count = params->data.list.count;
            
            // Allocate stack space for parameters (calculate after processing all params)
            int stack_space = 0;
            
            // First pass: analyze parameters and add to symbol table (don't store yet)
            int reg_index = 0;
            for (int i = 0; i < param_count && reg_index < 4; i++) {
                ASTNode *param = params->data.list.children[i];
                const char *param_name = NULL;
                const char *param_type = NULL;
                
                if (param->type == AST_IDENTIFIER) {
                    // Simple parameter: just the name
                    param_name = param->data.string_value;
                } else if (param->type == AST_LIST && param->data.list.count >= 2) {
                    // Parameter with type: (name type)
                    ASTNode *name_node = param->data.list.children[0];
                    ASTNode *type_node = param->data.list.children[1];
                    if (name_node->type == AST_IDENTIFIER) {
                        param_name = name_node->data.string_value;
                    }
                    if (type_node->type == AST_IDENTIFIER) {
                        param_type = type_node->data.string_value;
                    } else if (type_node->type == AST_LIST && type_node->data.list.count >= 1) {
                        // Check if this is an array type: ([] int 4)
                        ASTNode *first_child = type_node->data.list.children[0];
                        if (first_child->type == AST_IDENTIFIER && strcmp(first_child->data.string_value, "[]") == 0) {
                            param_type = "array"; // Mark as array type
                        }
                    }
                }
                
                if (param_name) {
                    // Check parameter type to determine how to handle it
                    if (param_type && (strcmp(param_type, "Point") == 0 || 
                                     strcmp(param_type, "struct") == 0 ||
                                     // Add more struct type names as needed
                                     (param_type[0] >= 'A' && param_type[0] <= 'Z'))) {
                        // This is a struct parameter - track register usage
                        reg_index += 2; // Struct uses 2 registers
                        
                        // Add struct symbol to function symbol table
                        Symbol symbol;
                        symbol.name = strdup(param_name);
                        symbol.type = SYM_STRUCT;
                        add_symbol(func_symbols, symbol);
                    } else if (param_type && (strstr(param_type, "[") != NULL || strcmp(param_type, "array") == 0)) {
                        // This is an array parameter - track register usage
                        reg_index += 4; // Array uses 4 registers
                        
                        // Add array symbol to function symbol table
                        Symbol symbol;
                        symbol.name = strdup(param_name);
                        symbol.type = SYM_ARRAY;
                        add_symbol(func_symbols, symbol);
                    } else {
                        // Regular parameter - track register usage
                        reg_index++;
                        
                        // Add to function symbol table
                        Symbol symbol;
                        symbol.name = strdup(param_name);
                        symbol.type = SYM_INT; // Default to int
                        add_symbol(func_symbols, symbol);
                    }
                }
            }
            
            // Calculate and allocate stack space based on parameter types
            for (size_t i = 0; i < func_symbols->count; i++) {
                if (func_symbols->symbols[i].type == SYM_ARRAY) {
                    stack_space += 32; // 4 elements × 8 bytes for arrays (assuming int[4])
                } else if (func_symbols->symbols[i].type == SYM_STRUCT) {
                    stack_space += 16; // 2 fields × 8 bytes for structs  
                } else {
                    stack_space += 8; // 8 bytes for regular variables
                }
            }
            if (stack_space % 16 != 0) {
                stack_space += 16 - (stack_space % 16);
            }
            if (stack_space > 0) {
                emit_code(codegen, "    sub   sp, sp, #%d\n", stack_space);
            }
            
            // Second pass: store parameters from registers to stack
            reg_index = 0;
            for (int i = 0; i < param_count && reg_index < 4; i++) {
                ASTNode *param = params->data.list.children[i];
                const char *param_name = NULL;
                const char *param_type = NULL;
                
                // Extract parameter info (same logic as first pass)
                if (param->type == AST_IDENTIFIER) {
                    param_name = param->data.string_value;
                } else if (param->type == AST_LIST && param->data.list.count >= 2) {
                    ASTNode *name_node = param->data.list.children[0];
                    ASTNode *type_node = param->data.list.children[1];
                    if (name_node->type == AST_IDENTIFIER) {
                        param_name = name_node->data.string_value;
                    }
                    if (type_node->type == AST_IDENTIFIER) {
                        param_type = type_node->data.string_value;
                    } else if (type_node->type == AST_LIST && type_node->data.list.count >= 1) {
                        ASTNode *first_child = type_node->data.list.children[0];
                        if (first_child->type == AST_IDENTIFIER && strcmp(first_child->data.string_value, "[]") == 0) {
                            param_type = "array";
                        }
                    }
                }
                
                if (param_name) {
                    // Store parameter based on type
                    if (param_type && (strcmp(param_type, "Point") == 0 || 
                                     strcmp(param_type, "struct") == 0 ||
                                     (param_type[0] >= 'A' && param_type[0] <= 'Z'))) {
                        // Store struct fields
                        emit_code(codegen, "    str   x%d, [sp, #%d]\n", reg_index, reg_index * 8);
                        reg_index++;
                        if (reg_index < 4) {
                            emit_code(codegen, "    str   x%d, [sp, #%d]\n", reg_index, reg_index * 8);
                            reg_index++;
                        }
                    } else if (param_type && (strstr(param_type, "[") != NULL || strcmp(param_type, "array") == 0)) {
                        // Store array elements
                        for (int elem = 0; elem < 4 && reg_index < 4; elem++) {
                            emit_code(codegen, "    str   x%d, [sp, #%d]\n", reg_index, reg_index * 8);
                            reg_index++;
                        }
                    } else {
                        // Store regular parameter
                        emit_code(codegen, "    str   x%d, [sp, #%d]\n", reg_index, reg_index * 8);
                        reg_index++;
                    }
                }
            }
        }
    }
    
    // Generate function body with function-local symbols
    if (fn_node->data.list.count >= 4) {
        ASTNode *body = fn_node->data.list.children[3];
        generate_statement(codegen, body, func_symbols);
    } else {
        // No body, return 0
        emit_mov_immediate(codegen, "x0", 0);
    }
    
    // Cleanup stack space for parameters
    if (func_symbols->count > 0) {
        int cleanup_space = 0;
        for (size_t i = 0; i < func_symbols->count; i++) {
            if (func_symbols->symbols[i].type == SYM_ARRAY) {
                cleanup_space += 32; // 4 elements × 8 bytes for arrays (assuming int[4])
            } else if (func_symbols->symbols[i].type == SYM_STRUCT) {
                cleanup_space += 16; // 2 fields × 8 bytes for structs  
            } else {
                cleanup_space += 8; // 8 bytes for regular variables
            }
        }
        if (cleanup_space % 16 != 0) {
            cleanup_space += 16 - (cleanup_space % 16);
        }
        if (cleanup_space > 0) {
            emit_code(codegen, "    add   sp, sp, #%d\n", cleanup_space);
        }
    }
    
    // Restore frame and return
    emit_code(codegen, "    ldp   x29, x30, [sp], #16\n");
    emit_code(codegen, "    ret\n");
    
    // Free function symbol table
    free_symbol_table(func_symbols);
    
    emit_code(codegen, "\n");
}

void generate_main_function(CodeGen *codegen, ASTNode *ast, SymbolTable *symbols) {
    // Generate main function
    emit_code(codegen, "_main:\n");
    
    // Setup frame pointer
    emit_code(codegen, "    stp   x29, x30, [sp, #-16]!\n");
    emit_code(codegen, "    mov   x29, sp\n");
    
    // Calculate stack space needed
    int stack_space = 0;
    for (size_t i = 0; i < symbols->count; i++) {
        if (symbols->symbols[i].type == SYM_ARRAY) {
            stack_space += 32; // 4 elements × 8 bytes for arrays (assuming int[4])
        } else if (symbols->symbols[i].type == SYM_STRUCT) {
            stack_space += 16; // 2 fields × 8 bytes for structs  
        } else {
            stack_space += 8; // 8 bytes for regular variables
        }
    }
    if (stack_space % 16 != 0) {
        stack_space += 16 - (stack_space % 16);
    }
    
    // Allocate stack space
    if (stack_space > 0) {
        emit_code(codegen, "    sub   sp, sp, #%d\n", stack_space);
    }
    
    // Generate code for each statement (skip function definitions)
    // Also find the final expression to use as exit code
    ASTNode *final_expr = NULL;
    if (ast->type == AST_LIST) {
        for (size_t i = 0; i < ast->data.list.count; i++) {
            ASTNode *stmt = ast->data.list.children[i];
            
            // Skip function definitions - they were already generated above
            bool is_function_def = false;
            if (stmt->type == AST_LIST && stmt->data.list.count >= 3) {
                ASTNode *op = stmt->data.list.children[0];
                if (op->type == AST_IDENTIFIER && strcmp(op->data.string_value, "let") == 0) {
                    ASTNode *init_value = (stmt->data.list.count >= 4) ? stmt->data.list.children[3] : NULL;
                    if (init_value && init_value->type == AST_LIST && 
                        init_value->data.list.count >= 3) {
                        ASTNode *fn_node = init_value->data.list.children[0];
                        if (fn_node->type == AST_IDENTIFIER && strcmp(fn_node->data.string_value, "fn") == 0) {
                            is_function_def = true;
                        }
                    }
                }
            }
            
            if (!is_function_def) {
                generate_statement(codegen, stmt, symbols);
                // If this is not a statement (no operator), it's an expression for exit code
                if (stmt->type == AST_IDENTIFIER || stmt->type == AST_INT || 
                    (stmt->type == AST_LIST && stmt->data.list.count > 0 && 
                     stmt->data.list.children[0]->type == AST_IDENTIFIER &&
                     (strcmp(stmt->data.list.children[0]->data.string_value, "let") != 0 &&
                      strcmp(stmt->data.list.children[0]->data.string_value, "set") != 0 &&
                      strcmp(stmt->data.list.children[0]->data.string_value, "print") != 0 &&
                      strcmp(stmt->data.list.children[0]->data.string_value, "if") != 0 &&
                      strcmp(stmt->data.list.children[0]->data.string_value, "while") != 0))) {
                    final_expr = stmt;
                }
            }
        }
    }
    
    // Set exit code based on final expression or default to 0
    if (final_expr) {
        generate_expression(codegen, final_expr, symbols);
    } else {
        emit_mov_immediate(codegen, "x0", 0);
    }
    
    // Deallocate stack space
    if (stack_space > 0) {
        emit_code(codegen, "    add   sp, sp, #%d\n", stack_space);
    }
    
    // Restore frame pointer and return address, then exit
    emit_code(codegen, "    ldp   x29, x30, [sp], #16\n");
    emit_code(codegen, "    ret\n");
}

char *compile_to_arm64(ASTNode *ast, SymbolTable *symbols, StructTypeTable *struct_types) {
    CodeGen *codegen = create_codegen();
    
    // Initialize global struct types table
    if (!global_struct_types) {
        global_struct_types = create_struct_type_table();
    }
    
    generate_preamble(codegen);
    
    // Only generate pow function if exponentiation is used
    if (uses_exponentiation(ast)) {
        generate_pow_function(codegen);
    }
    
    // Generate all function definitions first
    if (ast->type == AST_LIST) {
        for (size_t i = 0; i < ast->data.list.count; i++) {
            ASTNode *stmt = ast->data.list.children[i];
            
            if (stmt->type == AST_LIST && stmt->data.list.count >= 3) {
                ASTNode *op = stmt->data.list.children[0];
                
                if (op->type == AST_IDENTIFIER && strcmp(op->data.string_value, "let") == 0) {
                    ASTNode *name_node = stmt->data.list.children[1];
                    
                    // Check different let statement formats:
                    // 3 elements: (let name (fn ...))  
                    // 4 elements: (let name type value) or (let name type (fn ...))
                    ASTNode *init_value = NULL;
                    if (stmt->data.list.count == 3) {
                        init_value = stmt->data.list.children[2];
                    } else if (stmt->data.list.count >= 4) {
                        init_value = stmt->data.list.children[3];
                    }
                    
                    // Check if this is a function definition
                    if (init_value && init_value->type == AST_LIST && 
                        init_value->data.list.count >= 3) {
                        ASTNode *fn_node = init_value->data.list.children[0];
                        if (fn_node->type == AST_IDENTIFIER && strcmp(fn_node->data.string_value, "fn") == 0) {
                            generate_function_definition(codegen, name_node->data.string_value, init_value, symbols);
                        }
                    }
                }
            }
        }
    }
    
    generate_main_function(codegen, ast, symbols);
    
    // No longer need format strings since we use print helper functions
    
    char *result = strdup(codegen->output);
    free_codegen(codegen);
    
    return result;
}

// Struct type table management functions
StructTypeTable *create_struct_type_table() {
    StructTypeTable *table = malloc(sizeof(StructTypeTable));
    if (!table) {
        fprintf(stderr, "Error: Failed to allocate memory for struct type table\n");
        exit(1);
    }
    table->types = NULL;
    table->count = 0;
    table->capacity = 0;
    return table;
}

void add_struct_type(StructTypeTable *table, const char *name, StructField *fields, size_t field_count) {
    if (table->count >= table->capacity) {
        table->capacity = table->capacity == 0 ? 4 : table->capacity * 2;
        table->types = realloc(table->types, table->capacity * sizeof(StructType));
        if (!table->types) {
            fprintf(stderr, "Error: Failed to reallocate memory for struct types\n");
            exit(1);
        }
    }
    
    StructType *type = &table->types[table->count];
    type->name = strdup(name);
    type->fields = malloc(field_count * sizeof(StructField));
    type->field_count = field_count;
    type->field_capacity = field_count;
    
    // Copy fields
    for (size_t i = 0; i < field_count; i++) {
        type->fields[i].name = strdup(fields[i].name);
        type->fields[i].type = fields[i].type;
        type->fields[i].default_value = fields[i].default_value; // Shallow copy for now
    }
    
    table->count++;
}

StructType *find_struct_type(StructTypeTable *table, const char *name) {
    if (!table) return NULL;
    
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->types[i].name, name) == 0) {
            return &table->types[i];
        }
    }
    return NULL;
}

void free_struct_type_table(StructTypeTable *table) {
    if (!table) return;
    
    for (size_t i = 0; i < table->count; i++) {
        free(table->types[i].name);
        for (size_t j = 0; j < table->types[i].field_count; j++) {
            free(table->types[i].fields[j].name);
        }
        free(table->types[i].fields);
    }
    free(table->types);
    free(table);
}