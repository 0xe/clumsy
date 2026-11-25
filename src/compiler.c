#include "compiler.h"
#include "arena.h"
#include "symbol_table.h"
#include "vec.h"
#include <stdarg.h>
#include <stdint.h>

CodeGen *create_codegen(void) {
    CodeGen *codegen = arena_calloc(&g_arena_compile, 1, sizeof(CodeGen));

    codegen->output_capacity = 4096;
    codegen->output = arena_alloc(&g_arena_compile, codegen->output_capacity);

    codegen->output[0] = '\0';
    codegen->output_size = 0;
    codegen->label_counter = 0;
    codegen->strings = NULL;
    codegen->string_count = 0;
    codegen->string_capacity = 0;
    codegen->sp_displacement = 0;
    codegen->scratch_offset = 0;
    codegen->scratch_depth = 0;
    codegen->current_epilogue_label = NULL;
    codegen->loop_depth = 0;
    
    return codegen;
}

void free_codegen(CodeGen *codegen) {
    (void)codegen;
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
        size_t old_capacity = codegen->output_capacity;
        codegen->output_capacity *= 2;
        codegen->output = arena_grow(&g_arena_compile, codegen->output,
                                     old_capacity, codegen->output_capacity);
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
    char *label = arena_alloc(&g_arena_scratch, strlen(prefix) + 20);
    sprintf(label, ".%s_%d", prefix, codegen->label_counter);
    return label;
}

// Symbol table and struct type management moved to symbol_table.c

bool uses_exponentiation(ASTNode *node) {
    return ast_uses_exponentiation(node);
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
    if (value >= -256 && value <= 255) {
        emit_code(codegen, "    mov   %s, #%d\n", reg, value);
        return;
    }
    // Derive w register name from x register name
    const char *wreg = reg;
    if (reg[0] == 'x') {
        static char wbuf[4] = "w0";
        wbuf[1] = reg[1];
        wreg = wbuf;
    }
    uint32_t uval = (uint32_t)value;
    emit_code(codegen, "    movz  %s, #0x%x\n", wreg, uval & 0xFFFF);
    if ((uval & 0xFFFF0000) != 0) {
        emit_code(codegen, "    movk  %s, #0x%x, lsl #16\n", wreg, (uval >> 16) & 0xFFFF);
    }
    emit_code(codegen, "    sxtw  %s, %s\n", reg, wreg);
}

void emit_push_call_frame(CodeGen *codegen) {
    emit_code(codegen, "    stp   x29, x30, [sp, #-16]!\n");
    codegen->sp_displacement += 16;
}

void emit_pop_call_frame(CodeGen *codegen) {
    codegen->sp_displacement -= 16;
    emit_code(codegen, "    ldp   x29, x30, [sp], #16\n");
}

void emit_load_variable(CodeGen *codegen, const char *reg, const char *var_name, SymbolTable *symbols) {
    // Check if this is a global variable
    Symbol *sym = find_symbol_recursive(symbols, var_name);
    if (sym && sym->is_global) {
        // Global variable: use adrp/add to load from .data section
        emit_code(codegen, "    adrp  x9, _%s@PAGE\n", var_name);
        emit_code(codegen, "    add   x9, x9, _%s@PAGEOFF\n", var_name);
        emit_code(codegen, "    ldr   %s, [x9]\n", reg);
        return;
    }
    
    int offset = get_symbol_offset(symbols, var_name);
    if (offset >= 0) {
        emit_code(codegen, "    ldr   %s, [sp, #%d]\n", reg, offset + codegen->sp_displacement);
    } else {
        fprintf(stderr, "error: undeclared variable '%s'\n", var_name);
        exit(1);
    }
}

void emit_store_variable(CodeGen *codegen, const char *reg, const char *var_name, SymbolTable *symbols) {
    // Check if this is a global variable
    Symbol *sym = find_symbol_recursive(symbols, var_name);
    if (sym && sym->is_global) {
        // Global variable: use adrp/add to store to .data section
        emit_code(codegen, "    adrp  x9, _%s@PAGE\n", var_name);
        emit_code(codegen, "    add   x9, x9, _%s@PAGEOFF\n", var_name);
        emit_code(codegen, "    str   %s, [x9]\n", reg);
        return;
    }
    
    int offset = get_symbol_offset(symbols, var_name);
    if (offset >= 0) {
        emit_code(codegen, "    str   %s, [sp, #%d]\n", reg, offset + codegen->sp_displacement);
    } else {
        fprintf(stderr, "error: undeclared variable '%s'\n", var_name);
        exit(1);
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
    } else if (strcmp(op, "=") == 0) {
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
    } else if (strcmp(op, "&") == 0) {
        emit_code(codegen, "    and   %s, %s, %s\n", dest, src1, src2);
    } else if (strcmp(op, "|") == 0) {
        emit_code(codegen, "    orr   %s, %s, %s\n", dest, src1, src2);
    } else if (strcmp(op, ">>") == 0) {
        emit_code(codegen, "    lsr   %s, %s, %s\n", dest, src1, src2);
    } else if (strcmp(op, "<<") == 0) {
        emit_code(codegen, "    lsl   %s, %s, %s\n", dest, src1, src2);
    }
}

size_t intern_string(CodeGen *codegen, const char *raw_value) {
    size_t string_index = codegen->string_count;
    size_t str_len = strlen(raw_value);
    char *unquoted = arena_alloc(&g_arena_compile, str_len + 1);
    if (str_len >= 2 && raw_value[0] == '"' && raw_value[str_len - 1] == '"') {
        strncpy(unquoted, raw_value + 1, str_len - 2);
        unquoted[str_len - 2] = '\0';
    } else {
        strcpy(unquoted, raw_value);
    }
    if (codegen->string_count >= codegen->string_capacity) {
        size_t old_capacity = codegen->string_capacity;
        codegen->string_capacity = old_capacity == 0 ? 16 : old_capacity * 2;
        codegen->strings = arena_grow(&g_arena_compile, codegen->strings,
                                      old_capacity * sizeof(char *),
                                      codegen->string_capacity * sizeof(char *));
    }
    codegen->strings[codegen->string_count++] = unquoted;
    return string_index;
}

// Evaluate a single call argument into a scratch register without clobbering
// x0..x7 (so later args and the call itself see deterministic values).
static void emit_call_arg_eval(CodeGen *codegen, ASTNode *arg, SymbolTable *symbols, const char *scratch) {
    if (arg->type == AST_INT) {
        emit_mov_immediate(codegen, scratch, arg->data.int_value);
    } else if (arg->type == AST_CHAR) {
        emit_mov_immediate(codegen, scratch, (int)arg->data.char_value);
    } else if (arg->type == AST_IDENTIFIER) {
        Symbol *arg_sym = find_symbol(symbols, arg->data.string_value);
        if (arg_sym && (arg_sym->type == SYM_STRUCT || arg_sym->type == SYM_ARRAY)) {
            int off = get_symbol_offset(symbols, arg->data.string_value);
            emit_code(codegen, "    mov   %s, sp\n", scratch);
            emit_code(codegen, "    add   %s, %s, #%d\n", scratch, scratch, off + codegen->sp_displacement);
        } else {
            emit_load_variable(codegen, scratch, arg->data.string_value, symbols);
        }
    } else if (arg->type == AST_STRING) {
        size_t string_index = intern_string(codegen, arg->data.string_value);
        emit_code(codegen, "    adrp   %s, str_%zu@PAGE\n", scratch, string_index);
        emit_code(codegen, "    add    %s, %s, str_%zu@PAGEOFF\n", scratch, scratch, string_index);
    } else if (arg->type == AST_LIST) {
        generate_expression(codegen, arg, symbols);
        emit_code(codegen, "    mov   %s, x0\n", scratch);
    }
}

void emit_function_call(CodeGen *codegen, ASTNode *op, ASTNode *expr, SymbolTable *symbols) {
    Symbol *func_symbol = find_symbol_recursive(symbols, op->data.string_value);
    if (!func_symbol || (func_symbol->type != SYM_FUNCTION && func_symbol->type != SYM_C_FUNCTION)) {
        fprintf(stderr, "error: undeclared function '%s'\n", op->data.string_value);
        exit(1);
    }

    emit_code(codegen, "    // Function call: %s\n", op->data.string_value);

    int arg_count = expr->data.list.count - 1;
    if (arg_count < 0) arg_count = 0;

    // Detect variadic C calls: printf is always a libc variadic function, and
    // passing more args than the extern declares passes the extras as varargs.
    bool variadic = false;
    if (func_symbol->type == SYM_C_FUNCTION) {
        const char *cname = func_symbol->type_info.c_function.c_name;
        if (strcmp(cname, "printf") == 0 || arg_count > func_symbol->type_info.c_function.param_count) {
            variadic = true;
        }
    }

    if (variadic) {
        // macOS ARM64 variadic ABI: named args in x0..x7, variadic args on the
        // stack at [sp, #0].. (va_start reads them there). Spill every arg so the
        // first variadic arg lands at [sp, #0], then keep the block live across
        // the call. This mirrors the x8-zeroing stub shims used by the qbe,
        // llvm, and cranelift backends.
        int spill_size = arg_count * 8;
        if (spill_size % 16 != 0) spill_size += 16 - (spill_size % 16);

        if (spill_size > 0) {
            emit_code(codegen, "    sub   sp, sp, #%d\n", spill_size);
            codegen->sp_displacement += spill_size;
        }

        // arg0 (the named arg) goes to the last slot; args 1.. are the varargs
        // at [sp, #0], [sp, #8], ... so va_start finds the first one at [sp].
        for (int i = 0; i < arg_count && i < 8; i++) {
            ASTNode *arg = expr->data.list.children[i + 1];
            int slot = (i == 0 && arg_count > 1) ? (arg_count - 1) * 8 : (i > 0 ? (i - 1) * 8 : 0);
            emit_call_arg_eval(codegen, arg, symbols, "x9");
            emit_code(codegen, "    str   x9, [sp, #%d]\n", slot);
        }

        for (int i = 0; i < arg_count && i < 8; i++) {
            int slot = (i == 0 && arg_count > 1) ? (arg_count - 1) * 8 : (i > 0 ? (i - 1) * 8 : 0);
            emit_code(codegen, "    ldr   x%d, [sp, #%d]\n", i, slot);
        }

        // x8 = number of vector registers passed as varargs (must be 0 here)
        emit_code(codegen, "    mov   x8, #0\n");

        // Call the function (macOS ABI: _ prefix for all symbols)
        if (func_symbol->type == SYM_C_FUNCTION) {
            char *c_func_name = arena_alloc(&g_arena_scratch, strlen(func_symbol->type_info.c_function.c_name) + 2);
            sprintf(c_func_name, "_%s", func_symbol->type_info.c_function.c_name);
            emit_code(codegen, "    bl    %s\n", c_func_name);
        } else {
            emit_code(codegen, "    bl    _%s\n", op->data.string_value);
        }

        // Deallocate spill space AFTER the call (the callee reads it)
        if (spill_size > 0) {
            codegen->sp_displacement -= spill_size;
            emit_code(codegen, "    add   sp, sp, #%d\n", spill_size);
        }
        return;
    }

    // Evaluate all args into a scratch register, spilling each to the stack so
    // we don't clobber during later evals. Then move them into x0..x7.
    for (int i = 0; i < arg_count && i < 8; i++) {
        ASTNode *arg = expr->data.list.children[i + 1];
        emit_call_arg_eval(codegen, arg, symbols, "x9");
        emit_code(codegen, "    str   x9, [sp, #-16]!\n");
        codegen->sp_displacement += 16;
    }

    // Pop spilled args back into x0..x7 (reverse order)
    for (int i = arg_count - 1; i >= 0 && i < 8; i--) {
        emit_code(codegen, "    ldr   x%d, [sp], #16\n", i);
        codegen->sp_displacement -= 16;
    }

    // Call the function (macOS ABI: _ prefix for all symbols)
    if (func_symbol->type == SYM_C_FUNCTION) {
        char *c_func_name = arena_alloc(&g_arena_scratch, strlen(func_symbol->type_info.c_function.c_name) + 2);
        sprintf(c_func_name, "_%s", func_symbol->type_info.c_function.c_name);
        emit_code(codegen, "    bl    %s\n", c_func_name);
    } else {
        emit_code(codegen, "    bl    _%s\n", op->data.string_value);
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

        case AST_STRING: {
            size_t string_index = intern_string(codegen, expr->data.string_value);
            emit_code(codegen, "    adrp   x9, str_%zu@PAGE\n", string_index);
            emit_code(codegen, "    add    x9, x9, str_%zu@PAGEOFF\n", string_index);
            emit_code(codegen, "    mov    x0, x9\n");
            break;
        }

        case AST_IDENTIFIER:
            emit_load_variable(codegen, "x0", expr->data.string_value, symbols);
            break;
            
        case AST_LIST:
            if (expr->data.list.count > 0) {
                ASTNode *op = expr->data.list.children[0];
                
                if (op->type == AST_IDENTIFIER) {
                    // Handle logical OR: (|| a b) — short-circuit
                    if (strcmp(op->data.string_value, "||") == 0 && expr->data.list.count == 3) {
                        ASTNode *left = expr->data.list.children[1];
                        ASTNode *right = expr->data.list.children[2];
                        char *true_label = new_label(codegen, "or_true");
                        char *end_label = new_label(codegen, "or_end");

                        generate_expression(codegen, left, symbols);
                        emit_code(codegen, "    cbnz   x0, %s\n", true_label);

                        generate_expression(codegen, right, symbols);
                        emit_code(codegen, "    cbnz   x0, %s\n", true_label);

                        emit_mov_immediate(codegen, "x0", 0);
                        emit_code(codegen, "    b     %s\n", end_label);

                        emit_code(codegen, "%s:\n", true_label);
                        emit_mov_immediate(codegen, "x0", 1);
                        emit_code(codegen, "%s:\n", end_label);
                    }
                    // Handle logical AND: (&& a b) — short-circuit
                    else if (strcmp(op->data.string_value, "&&") == 0 && expr->data.list.count == 3) {
                        ASTNode *left = expr->data.list.children[1];
                        ASTNode *right = expr->data.list.children[2];
                        char *false_label = new_label(codegen, "and_false");
                        char *end_label = new_label(codegen, "and_end");

                        generate_expression(codegen, left, symbols);
                        emit_code(codegen, "    cbz    x0, %s\n", false_label);

                        generate_expression(codegen, right, symbols);
                        emit_code(codegen, "    cbz    x0, %s\n", false_label);

                        emit_mov_immediate(codegen, "x0", 1);
                        emit_code(codegen, "    b     %s\n", end_label);

                        emit_code(codegen, "%s:\n", false_label);
                        emit_mov_immediate(codegen, "x0", 0);
                        emit_code(codegen, "%s:\n", end_label);
                    }
                    // Handle logical not: (! expr)
                    else if (strcmp(op->data.string_value, "!") == 0 && expr->data.list.count == 2) {
                        ASTNode *operand = expr->data.list.children[1];
                        generate_expression(codegen, operand, symbols);
                        emit_code(codegen, "    cmp   x0, #0\n");
                        emit_code(codegen, "    cset  x0, eq\n");
                    }
                    // Handle arithmetic, comparison, and bitwise operators
                    else if (strcmp(op->data.string_value, "+") == 0 ||
                        strcmp(op->data.string_value, "-") == 0 ||
                        strcmp(op->data.string_value, "*") == 0 ||
                        strcmp(op->data.string_value, "/") == 0 ||
                        strcmp(op->data.string_value, "%") == 0 ||
                        strcmp(op->data.string_value, "**") == 0 ||
                        strcmp(op->data.string_value, "=") == 0 ||
                        strcmp(op->data.string_value, "<") == 0 ||
                        strcmp(op->data.string_value, ">") == 0 ||
                        strcmp(op->data.string_value, "<=") == 0 ||
                        strcmp(op->data.string_value, ">=") == 0 ||
                        strcmp(op->data.string_value, "&") == 0 ||
                        strcmp(op->data.string_value, "|") == 0 ||
                        strcmp(op->data.string_value, ">>") == 0 ||
                        strcmp(op->data.string_value, "<<") == 0) {
                        
                        if (expr->data.list.count >= 3) {
                            // Generate code for operands
                            ASTNode *left = expr->data.list.children[1];
                            ASTNode *right = expr->data.list.children[2];
                            
                            // Check if right operand is a complex expression that might clobber x2
                            bool right_is_complex = (right->type == AST_LIST);
                            
                            if (right_is_complex) {
                                // Evaluate left into x0
                                if (left->type == AST_INT) {
                                    emit_mov_immediate(codegen, "x0", left->data.int_value);
                                } else if (left->type == AST_IDENTIFIER) {
                                    emit_load_variable(codegen, "x0", left->data.string_value, symbols);
                                } else if (left->type == AST_LIST) {
                                    generate_expression(codegen, left, symbols);
                                }
                                
                                // Spill left to per-depth scratch slot
                                int slot = codegen->scratch_offset + 8 * codegen->scratch_depth;
                                emit_code(codegen, "    str   x0, [sp, #%d]\n", slot);
                                codegen->scratch_depth++;
                                
                                // Evaluate right into x3
                                generate_expression(codegen, right, symbols);
                                emit_code(codegen, "    mov   x3, x0\n");
                                
                                // Reload left from scratch slot
                                codegen->scratch_depth--;
                                slot = codegen->scratch_offset + 8 * codegen->scratch_depth;
                                emit_code(codegen, "    ldr   x2, [sp, #%d]\n", slot);
                            } else {
                                // Simple right: safe to load left into x2 first
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
                        // Handle dereference: (* ptr) — load value from address in ptr
                        else if (strcmp(op->data.string_value, "*") == 0 && expr->data.list.count == 2) {
                            ASTNode *operand = expr->data.list.children[1];
                            generate_expression(codegen, operand, symbols);
                            emit_code(codegen, "    ldr   x0, [x0]\n");
                        }
                        // Handle address-of: (& x) — compute stack address of x
                        else if (strcmp(op->data.string_value, "&") == 0 && expr->data.list.count == 2) {
                            ASTNode *operand = expr->data.list.children[1];
                            if (operand->type == AST_IDENTIFIER) {
                                Symbol *sym = find_symbol_recursive(symbols, operand->data.string_value);
                                if (sym && (sym->is_global || sym->is_external)) {
                                    // Global variable: address via adrp/add
                                    emit_code(codegen, "    adrp  x0, _%s@PAGE\n", operand->data.string_value);
                                    emit_code(codegen, "    add   x0, x0, _%s@PAGEOFF\n", operand->data.string_value);
                                } else {
                                    int offset = get_symbol_offset(symbols, operand->data.string_value);
                                    if (offset >= 0) {
                                        emit_code(codegen, "    mov   x0, sp\n");
                                        emit_code(codegen, "    add   x0, x0, #%d\n", offset + codegen->sp_displacement);
                                    } else {
                                        emit_mov_immediate(codegen, "x0", 0);
                                    }
                                }
                            } else {
                                emit_mov_immediate(codegen, "x0", 0);
                            }
                        }
                    }
                    // Handle logical not: (not expr)
                    else if (strcmp(op->data.string_value, "not") == 0 && expr->data.list.count == 2) {
                        ASTNode *operand = expr->data.list.children[1];
                        generate_expression(codegen, operand, symbols);
                        emit_code(codegen, "    cmp   x0, #0\n");
                        emit_code(codegen, "    cset  x0, eq\n");
                    }
                    // Handle bitwise not: (~ expr)
                    else if (strcmp(op->data.string_value, "~") == 0 && expr->data.list.count == 2) {
                        ASTNode *operand = expr->data.list.children[1];
                        generate_expression(codegen, operand, symbols);
                        emit_code(codegen, "    mvn   x0, x0\n");
                        emit_code(codegen, "    and   x0, x0, #0xFFFFFFFF\n");
                    }
                    // Handle array access: ([] array_var index)
                    else if (strcmp(op->data.string_value, "[]") == 0) {
                        if (expr->data.list.count >= 3) {
                            ASTNode *array_var = expr->data.list.children[1];
                            ASTNode *index = expr->data.list.children[2];
                            
                            if (array_var->type == AST_IDENTIFIER) {
                                Symbol *sym = find_symbol_recursive(symbols, array_var->data.string_value);
                                
                                if (sym && (sym->is_global || sym->is_external)) {
                                    // Global array: compute index first, then load base
                                    if (index->type == AST_INT) {
                                        int element_offset = index->data.int_value * 8;
                                        emit_code(codegen, "    adrp  x9, _%s@PAGE\n", array_var->data.string_value);
                                        emit_code(codegen, "    add   x9, x9, _%s@PAGEOFF\n", array_var->data.string_value);
                                        emit_code(codegen, "    ldr   x0, [x9, #%d]\n", element_offset);
                                    } else {
                                        // Compute index into x1 first
                                        if (index->type == AST_IDENTIFIER) {
                                            emit_load_variable(codegen, "x1", index->data.string_value, symbols);
                                        } else {
                                            generate_expression(codegen, index, symbols);
                                            emit_code(codegen, "    mov   x1, x0\n");
                                        }
                                        // Now load base address (safe, index is already in x1)
                                        emit_code(codegen, "    adrp  x9, _%s@PAGE\n", array_var->data.string_value);
                                        emit_code(codegen, "    add   x9, x9, _%s@PAGEOFF\n", array_var->data.string_value);
                                        emit_code(codegen, "    mov   x10, #8\n");
                                        emit_code(codegen, "    mul   x1, x1, x10\n");
                                        emit_code(codegen, "    add   x9, x9, x1\n");
                                        emit_code(codegen, "    ldr   x0, [x9]\n");
                                    }
                                } else {
                                    // Local stack array
                                    int array_offset = get_symbol_offset(symbols, array_var->data.string_value);
                                    
                                    if (array_offset >= 0) {
                                        if (index->type == AST_INT) {
                                            int element_offset = index->data.int_value * 8;
                                            emit_code(codegen, "    ldr   x0, [sp, #%d]\n", array_offset + element_offset + codegen->sp_displacement);
                                        } else {
                                            if (index->type == AST_IDENTIFIER) {
                                                emit_load_variable(codegen, "x1", index->data.string_value, symbols);
                                            } else {
                                                generate_expression(codegen, index, symbols);
                                                emit_code(codegen, "    mov   x1, x0\n");
                                            }
                                            emit_code(codegen, "    mov   x9, #8\n");
                                            emit_code(codegen, "    mul   x1, x1, x9\n");
                                            emit_code(codegen, "    mov   x9, sp\n");
                                            emit_code(codegen, "    add   x9, x9, #%d\n", array_offset + codegen->sp_displacement);
                                            emit_code(codegen, "    add   x9, x9, x1\n");
                                            emit_code(codegen, "    ldr   x0, [x9]\n");
                                        }
                                    } else {
                                        emit_mov_immediate(codegen, "x0", 0);
                                    }
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
                                    StructType *struct_type = find_struct_type(get_global_struct_types(), struct_type_name);
                                    if (struct_type) {
                                        // Find the field in the struct type definition
                                        const char *field = field_name->data.string_value;
                                        int field_offset = -1;
                                        
                                        for (size_t i = 0; i < struct_type->count; i++) {
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
                        if (func_symbol && (func_symbol->type == SYM_FUNCTION || func_symbol->type == SYM_C_FUNCTION)) {
                            emit_function_call(codegen, op, expr, symbols);
                        } else {
                            fprintf(stderr, "error: undeclared function '%s'\n", op->data.string_value);
                            exit(1);
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
                            StructField *fields = arena_calloc(&g_arena_scratch, field_count, sizeof(StructField));
                            
                            for (size_t i = 0; i < field_count; i++) {
                                ASTNode *field = fields_list->data.list.children[i];
                                if (field->type == AST_LIST && field->data.list.count >= 3) {
                                    ASTNode *field_name = field->data.list.children[0];
                                    ASTNode *field_type = field->data.list.children[1];
                                    ASTNode *field_default = field->data.list.children[2];
                                    
                                    if (field_name->type == AST_IDENTIFIER && field_type->type == AST_IDENTIFIER) {
                                        fields[i].name = arena_strdup(&g_arena_scratch, field_name->data.string_value);
                                        
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
                            
                            if (!get_global_struct_types()) create_struct_type_table();
                            add_struct_type(get_global_struct_types(), name_node->data.string_value, fields, field_count);
                        }
                    }
                    return; // Don't generate code for struct type definitions
                }
                
                // Check if this is a struct or array initialization
                if (init->type == AST_ARRAY) {
                    // Handle AST_ARRAY type: [1 2 3 4]
                    int base_offset = get_symbol_offset(symbols, name_node->data.string_value);
                    Symbol *arr_sym = find_symbol(symbols, name_node->data.string_value);
                    int declared_size = (arr_sym && arr_sym->type == SYM_ARRAY) ? arr_sym->type_info.array.size : (int)init->data.list.count;
                    for (size_t i = 0; i < init->data.list.count; i++) {
                        ASTNode *element = init->data.list.children[i];
                        generate_expression(codegen, element, symbols);
                        // Store each element at base + element_index * 8
                        emit_code(codegen, "    str   x0, [sp, #%d]\n", base_offset + (int)i * 8);
                    }
                    // Zero-fill remaining elements
                    for (size_t i = init->data.list.count; i < (size_t)declared_size; i++) {
                        emit_code(codegen, "    str   xzr, [sp, #%d]\n", base_offset + (int)i * 8);
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
                            if (get_global_struct_types() && find_struct_type(get_global_struct_types(), type_name)) {
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
        // Variable assignment: (set name value) or (set ([] arr index) value)
        if (stmt->data.list.count >= 3) {
            ASTNode *name_node = stmt->data.list.children[1];
            ASTNode *value = stmt->data.list.children[2];
            
            generate_expression(codegen, value, symbols);
            
            // Check if target is array element: (set ([] arr index) value)
            if (name_node->type == AST_LIST && name_node->data.list.count >= 3) {
                ASTNode *access_op = name_node->data.list.children[0];
                if (access_op->type == AST_IDENTIFIER && strcmp(access_op->data.string_value, "[]") == 0) {
                    ASTNode *array_var = name_node->data.list.children[1];
                    ASTNode *index = name_node->data.list.children[2];
                    
                    Symbol *arr_sym = find_symbol_recursive(symbols, array_var->data.string_value);
                    
                    if (arr_sym && (arr_sym->is_global || arr_sym->is_external)) {
                        // Global array store: compute index first, then load base
                        if (index->type == AST_INT) {
                            emit_code(codegen, "    adrp  x9, _%s@PAGE\n", array_var->data.string_value);
                            emit_code(codegen, "    add   x9, x9, _%s@PAGEOFF\n", array_var->data.string_value);
                            emit_code(codegen, "    str   x0, [x9, #%d]\n", index->data.int_value * 8);
                        } else {
                            // Save value to store
                            emit_code(codegen, "    mov   x10, x0\n");
                            // Compute index into x1 first
                            if (index->type == AST_IDENTIFIER) {
                                emit_load_variable(codegen, "x1", index->data.string_value, symbols);
                            } else {
                                generate_expression(codegen, index, symbols);
                                emit_code(codegen, "    mov   x1, x0\n");
                            }
                            // Now load base address (safe, index is in x1, value in x10)
                            emit_code(codegen, "    adrp  x9, _%s@PAGE\n", array_var->data.string_value);
                            emit_code(codegen, "    add   x9, x9, _%s@PAGEOFF\n", array_var->data.string_value);
                            emit_code(codegen, "    mov   x11, #8\n");
                            emit_code(codegen, "    mul   x1, x1, x11\n");
                            emit_code(codegen, "    add   x9, x9, x1\n");
                            emit_code(codegen, "    str   x10, [x9]\n");
                        }
                    } else {
                        // Local stack array store
                        int array_offset = get_symbol_offset(symbols, array_var->data.string_value);
                        
                        if (index->type == AST_INT) {
                            emit_code(codegen, "    str   x0, [sp, #%d]\n", array_offset + index->data.int_value * 8 + codegen->sp_displacement);
                        } else {
                            emit_code(codegen, "    mov   x10, x0\n");
                            
                            if (index->type == AST_IDENTIFIER) {
                                emit_load_variable(codegen, "x1", index->data.string_value, symbols);
                            } else {
                                generate_expression(codegen, index, symbols);
                                emit_code(codegen, "    mov   x1, x0\n");
                            }
                            
                            emit_code(codegen, "    mov   x9, #8\n");
                            emit_code(codegen, "    mul   x1, x1, x9\n");
                            emit_code(codegen, "    mov   x9, sp\n");
                            emit_code(codegen, "    add   x9, x9, #%d\n", array_offset + codegen->sp_displacement);
                            emit_code(codegen, "    add   x9, x9, x1\n");
                            emit_code(codegen, "    str   x10, [x9]\n");
                        }
                    }
                    return;
                }
            }
            
            emit_store_variable(codegen, "x0", name_node->data.string_value, symbols);
        }
    } else if (strcmp(op->data.string_value, "print") == 0) {
        // Print statement: (print expr) — emits printf with appropriate format string
        // macOS ARM64 variadic ABI: format string (named param) in x0, variadic args on stack at [sp]
        if (stmt->data.list.count >= 2) {
            ASTNode *expr = stmt->data.list.children[1];

            // Determine format string
            size_t fmt_index;
            if (expr->type == AST_STRING) {
                fmt_index = intern_string(codegen, "%s");
            } else if (expr->type == AST_CHAR) {
                fmt_index = intern_string(codegen, "%c");
            } else {
                fmt_index = intern_string(codegen, "%ld");
            }

            // Evaluate expression into x9 (temp)
            generate_expression(codegen, expr, symbols);
            emit_code(codegen, "    mov   x9, x0\n");

            // Load format string into x0 (named param)
            emit_code(codegen, "    adrp  x0, str_%zu@PAGE\n", fmt_index);
            emit_code(codegen, "    add   x0, x0, str_%zu@PAGEOFF\n", fmt_index);

            // Push call frame, allocate space for variadic arg, store it on stack
            emit_push_call_frame(codegen);
            emit_code(codegen, "    sub   sp, sp, #16\n");
            codegen->sp_displacement += 16;
            emit_code(codegen, "    str   x9, [sp]\n");

            // Call printf (x8 = 0: no vector registers passed as varargs)
            emit_code(codegen, "    mov   x8, #0\n");
            emit_code(codegen, "    bl    _printf\n");

            // Restore stack
            emit_code(codegen, "    add   sp, sp, #16\n");
            codegen->sp_displacement -= 16;
            emit_pop_call_frame(codegen);
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
        }
    } else if (strcmp(op->data.string_value, "while") == 0) {
        // While loop: (while condition body)
        if (stmt->data.list.count >= 3) {
            ASTNode *condition = stmt->data.list.children[1];
            ASTNode *body = stmt->data.list.children[2];

            char *loop_label = new_label(codegen, "loop");
            char *end_label = new_label(codegen, "end_loop");

            // Push loop context for break/continue
            codegen->loop_stack[codegen->loop_depth].break_label = end_label;
            codegen->loop_stack[codegen->loop_depth].continue_label = loop_label;
            codegen->loop_depth++;

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

            // Pop loop context
            codegen->loop_depth--;
        }
    } else if (strcmp(op->data.string_value, "begin") == 0) {
        // Begin block: (begin stmt1 stmt2 ...)
        // Use pre-built scope if available, otherwise create one on the fly
        SymbolTable *child = stmt->scope;
        if (!child) {
            child = create_child_scope(symbols);
            stmt->scope = child;
            for (size_t i = 1; i < stmt->data.list.count; i++) {
                collect_symbols_from_stmt(stmt->data.list.children[i], child);
            }
        }
        for (size_t i = 1; i < stmt->data.list.count; i++) {
            generate_statement(codegen, stmt->data.list.children[i], child);
        }
    } else if (strcmp(op->data.string_value, "break") == 0) {
        // Break statement: (break) — jump to end of current loop
        if (codegen->loop_depth > 0) {
            LoopContext *ctx = &codegen->loop_stack[codegen->loop_depth - 1];
            emit_code(codegen, "    b     %s\n", ctx->break_label);
        } else {
            fprintf(stderr, "error: 'break' outside of loop\n");
            exit(1);
        }
    } else if (strcmp(op->data.string_value, "continue") == 0) {
        // Continue statement: (continue) — jump back to loop condition
        if (codegen->loop_depth > 0) {
            LoopContext *ctx = &codegen->loop_stack[codegen->loop_depth - 1];
            emit_code(codegen, "    b     %s\n", ctx->continue_label);
        } else {
            fprintf(stderr, "error: 'continue' outside of loop\n");
            exit(1);
        }
    } else if (strcmp(op->data.string_value, "ret") == 0) {
        // Return statement: (ret expr)
        if (stmt->data.list.count >= 2) {
            ASTNode *expr = stmt->data.list.children[1];
            generate_expression(codegen, expr, symbols);
        } else {
            // Return void
            emit_mov_immediate(codegen, "x0", 0);
        }
        if (codegen->current_epilogue_label) {
            emit_code(codegen, "    b     %s\n", codegen->current_epilogue_label);
        }
    } else {
        // Check if this is a function call
        if (stmt->type == AST_LIST && stmt->data.list.count > 0) {
            ASTNode *function_name = stmt->data.list.children[0];
            if (function_name->type == AST_IDENTIFIER) {
                Symbol *func_symbol = find_symbol(symbols, function_name->data.string_value);
                if (func_symbol && (func_symbol->type == SYM_FUNCTION || func_symbol->type == SYM_C_FUNCTION)) {
                    emit_function_call(codegen, function_name, stmt, symbols);
                    return;
                }
            }
        }
        
        // Other expressions
        generate_expression(codegen, stmt, symbols);
    }
}

void generate_function_definition(CodeGen *codegen, const char *func_name, ASTNode *fn_node, SymbolTable *symbols, bool is_public) {
    // Generate function label with .globl for cross-file linking (macOS ABI: _ prefix)
    if (is_public) {
        emit_code(codegen, ".globl _%s\n", func_name);
    }
    emit_code(codegen, "_%s:\n", func_name);
    
    // Setup frame
    emit_code(codegen, "    stp   x29, x30, [sp, #-16]!\n");
    emit_code(codegen, "    mov   x29, sp\n");
    
    // Create function-local symbol table
    SymbolTable *func_symbols = arena_calloc(&g_arena_compile, 1, sizeof(SymbolTable));
    func_symbols->symbols = NULL;
    func_symbols->count = 0;
    func_symbols->capacity = 0;
    func_symbols->parent = symbols;
    func_symbols->first_child = NULL;
    func_symbols->next_sibling = NULL;
    func_symbols->next_offset = 0;
    
    // Extract parameters and create local symbols
    int param_count = 0;
    if (fn_node->type == AST_FN) {
        param_count = fn_node->data.fn.param_count;
        ASTNode **params = fn_node->data.fn.params;
            
            // Allocate stack space for parameters (calculate after processing all params)
            int stack_space = 0;
            
            // First pass: analyze parameters and add to symbol table (don't store yet)
            int reg_index = 0;
            for (int i = 0; i < param_count && reg_index < 8; i++) {
                ASTNode *param = params[i];
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
                        Symbol symbol = {0};
                        symbol.name = arena_strdup(&g_arena_compile, param_name);
                        symbol.type = SYM_STRUCT;
                        add_symbol(func_symbols, symbol);
                    } else if (param_type && (strstr(param_type, "[") != NULL || strcmp(param_type, "array") == 0)) {
                        // This is an array parameter - track register usage
                        reg_index += 4; // Array uses 4 registers
                        
                        // Add array symbol to function symbol table
                        Symbol symbol = {0};
                        symbol.name = arena_strdup(&g_arena_compile, param_name);
                        symbol.type = SYM_ARRAY;
                        add_symbol(func_symbols, symbol);
                    } else {
                        // Regular parameter - track register usage
                        reg_index++;
                        
                        // Add to function symbol table
                        Symbol symbol = {0};
                        symbol.name = arena_strdup(&g_arena_compile, param_name);
                        symbol.type = SYM_INT; // Default to int
                        add_symbol(func_symbols, symbol);
                    }
                }
            }
            
            // Pre-build child scopes for fn body (for accurate stack sizing)
            if (fn_node->data.fn.body) {
                collect_symbols_from_stmt(fn_node->data.fn.body, func_symbols);
            }
            
            // Calculate and allocate stack space (includes nested scopes)
            stack_space = total_tree_size(func_symbols);
            codegen->scratch_offset = stack_space;
            stack_space += 128;
            if (stack_space % 16 != 0) {
                stack_space += 16 - (stack_space % 16);
            }
            if (stack_space > 0) {
                emit_code(codegen, "    sub   sp, sp, #%d\n", stack_space);
            }
            
            // Second pass: store parameters from registers to stack
            reg_index = 0;
            for (int i = 0; i < param_count && reg_index < 8; i++) {
                ASTNode *param = params[i];
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
                        if (reg_index < 8) {
                            emit_code(codegen, "    str   x%d, [sp, #%d]\n", reg_index, reg_index * 8);
                            reg_index++;
                        }
                    } else if (param_type && (strstr(param_type, "[") != NULL || strcmp(param_type, "array") == 0)) {
                        // Store array elements
                        for (int elem = 0; elem < 4 && reg_index < 8; elem++) {
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
    
    // Generate function body with function-local symbols
    char *epilogue_label = new_label(codegen, "epilogue");
    char *prev_epilogue = codegen->current_epilogue_label;
    codegen->current_epilogue_label = epilogue_label;
    
    if (fn_node->type == AST_FN && fn_node->data.fn.body) {
        generate_statement(codegen, fn_node->data.fn.body, func_symbols);
    } else {
        // No body, return 0
        emit_mov_immediate(codegen, "x0", 0);
    }
    
    codegen->current_epilogue_label = prev_epilogue;
    
    // Emit epilogue label (jump target for ret)
    emit_code(codegen, "%s:\n", epilogue_label);
    
    // Cleanup stack space (including nested scopes)
    {
        int cleanup_space = total_tree_size(func_symbols) + 128;
        if (cleanup_space % 16 != 0) {
            cleanup_space += 16 - (cleanup_space % 16);
        }
        if (cleanup_space > 0) {
            if (cleanup_space <= 4095) {
                emit_code(codegen, "    add   sp, sp, #%d\n", cleanup_space);
            } else {
                emit_mov_immediate(codegen, "x9", cleanup_space);
                emit_code(codegen, "    add   sp, sp, x9\n");
            }
        }
    }
    
    // Restore frame and return
    emit_code(codegen, "    ldp   x29, x30, [sp], #16\n");
    emit_code(codegen, "    ret\n");
    
    // Free function symbol table
    free_symbol_table(func_symbols);
    
    emit_code(codegen, "\n");
}

// Helper function to check if an AST node is a function call
bool is_function_call(ASTNode *node, SymbolTable *symbols) {
    if (node->type == AST_LIST && node->data.list.count > 0) {
        ASTNode *first = node->data.list.children[0];
        if (first->type == AST_IDENTIFIER) {
            Symbol *func_symbol = find_symbol(symbols, first->data.string_value);
            return func_symbol && (func_symbol->type == SYM_FUNCTION || func_symbol->type == SYM_C_FUNCTION);
        }
    }
    return false;
}

char *compile_to_arm64(ASTNode *ast, SymbolTable *symbols, StructTypeTable *struct_types) {
    (void)struct_types;
    CodeGen *codegen = create_codegen();
    if (!get_global_struct_types()) create_struct_type_table();
    
    generate_preamble(codegen);
    
    // Only generate pow function if exponentiation is used
    if (uses_exponentiation(ast)) {
        generate_pow_function(codegen);
    }
    
    // Process top-level struct definitions first (needed for struct type table)
    if (ast->type == AST_LIST) {
        for (size_t i = 0; i < ast->data.list.count; i++) {
            ASTNode *stmt = ast->data.list.children[i];
            if (stmt->type == AST_LIST && stmt->data.list.count >= 4) {
                ASTNode *op = stmt->data.list.children[0];
                if (op->type == AST_IDENTIFIER && (strcmp(op->data.string_value, "let") == 0 ||
                                                  strcmp(op->data.string_value, "pub") == 0)) {
                    ASTNode *type_node = stmt->data.list.children[2];
                    if (type_node->type == AST_IDENTIFIER && strcmp(type_node->data.string_value, "struct") == 0) {
                        generate_statement(codegen, stmt, symbols);
                    }
                }
            }
        }
    }
    
// Generate all function definitions first
    if (ast->type == AST_LIST) {
        for (size_t i = 0; i < ast->data.list.count; i++) {
            ASTNode *stmt = ast->data.list.children[i];
            
            if (stmt->type == AST_LIST && stmt->data.list.count >= 3) {
                ASTNode *op = stmt->data.list.children[0];
                
                if (op->type == AST_IDENTIFIER && (strcmp(op->data.string_value, "let") == 0 ||
                                                  strcmp(op->data.string_value, "pub") == 0)) {
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
                    if (init_value && init_value->type == AST_FN) {
                        bool is_pub = (strcmp(op->data.string_value, "pub") == 0);
                        generate_function_definition(codegen, name_node->data.string_value, init_value, symbols, is_pub);
                    }
                }
            }
        }
    }
    
    // Generate .data section for global variables
    if (ast->type == AST_LIST) {
        bool data_section_open = false;
        for (size_t i = 0; i < ast->data.list.count; i++) {
            ASTNode *stmt = ast->data.list.children[i];
            if (stmt->type == AST_LIST && stmt->data.list.count >= 4) {
                ASTNode *op = stmt->data.list.children[0];
                if (op->type == AST_IDENTIFIER && (strcmp(op->data.string_value, "let") == 0 ||
                                                  strcmp(op->data.string_value, "pub") == 0)) {
                    ASTNode *name_node = stmt->data.list.children[1];
                    ASTNode *type_node = stmt->data.list.children[2];
                    ASTNode *init_value = stmt->data.list.children[3];
                    
                    // Skip function definitions (handled above)
                    if (init_value->type == AST_FN) continue;
                    
                    // Skip struct type definitions
                    if (type_node->type == AST_IDENTIFIER && strcmp(type_node->data.string_value, "struct") == 0) continue;
                    
                    // This is a global variable
                    if (!data_section_open) {
                        emit_code(codegen, "\n    .data\n");
                        data_section_open = true;
                    }
                    
                    bool is_pub = (strcmp(op->data.string_value, "pub") == 0);
                    if (is_pub) {
                        emit_code(codegen, ".globl _%s\n", name_node->data.string_value);
                    }
                    emit_code(codegen, "_%s:\n", name_node->data.string_value);
                    
                    // Emit initial value
                    if (init_value->type == AST_INT) {
                        emit_code(codegen, "    .word   %d\n", init_value->data.int_value);
                        emit_code(codegen, "    .zero   4\n");  // pad to 8 bytes
                    } else if (init_value->type == AST_STRING) {
                        size_t si = intern_string(codegen, init_value->data.string_value);
                        emit_code(codegen, "    .asciz  \"%s\"\n", codegen->strings[si]);
                    } else if (init_value->type == AST_ARRAY) {
                        // Array initialization: emit each element as .quad
                        for (size_t j = 0; j < init_value->data.list.count; j++) {
                            ASTNode *elem = init_value->data.list.children[j];
                            if (elem->type == AST_INT) {
                                emit_code(codegen, "    .quad   %d\n", elem->data.int_value);
                            } else {
                                emit_code(codegen, "    .quad   0\n");
                            }
                        }
                        // Zero-fill remaining elements if array type with size
                        if (type_node->type == AST_LIST && type_node->data.list.count >= 3) {
                            ASTNode *size_node = type_node->data.list.children[2];
                            if (size_node->type == AST_INT) {
                                int declared_size = size_node->data.int_value;
                                for (size_t j = init_value->data.list.count; j < (size_t)declared_size; j++) {
                                    emit_code(codegen, "    .quad   0\n");
                                }
                            }
                        }
                    } else {
                        emit_code(codegen, "    .quad   0\n");
                    }
                }
            }
        }
        if (data_section_open) {
            emit_code(codegen, "    .text\n");
        }
    }
    
    // Emit all collected string literals
    if (codegen->string_count > 0) {
        emit_code(codegen, "\n    .data\n");
        for (size_t i = 0; i < codegen->string_count; i++) {
            emit_code(codegen, "str_%zu:\n", i);
            emit_code(codegen, "    .asciz  \"%s\"\n", codegen->strings[i]);
        }
        emit_code(codegen, "    .text\n");
    }
    
    char *result = arena_strdup(&g_arena_compile, codegen->output);
    free_codegen(codegen);
    
    return result;
}

