#include "ir.h"
#include "arena.h"
#include "symbol_table.h"

// IR Builder context
typedef struct {
    IRProgram *prog;
    IRFunction *fn;       // current function being built
    IRBlock *block;       // current block being emitted to

    // Variable tracking: maps variable names to their virtual register names
    // For stack locals: the register holds the alloc'd pointer
    struct {
        char *name;
        char *reg;         // virtual register holding pointer to the variable
        bool is_global;    // global vs local
        Symbol *symbol;    // original symbol for type info
    } *vars;
    int num_vars;
    int var_capacity;

    // Loop context for break/continue
    struct {
        char *break_label;
        char *continue_label;
    } loop_stack[64];
    int loop_depth;

    // String literal counter
    int string_counter;

    // Temp register counter
    int temp_counter;
} IRBuilder;

static char *ir_gen_temp(IRBuilder *b) {
    char buf[32];
    snprintf(buf, sizeof(buf), "t%d", b->temp_counter++);
    return arena_strdup(&g_arena_scratch, buf);
}

static char *ir_gen_string_label(IRBuilder *b) {
    char buf[32];
    snprintf(buf, sizeof(buf), "str_%d", b->string_counter++);
    return arena_strdup(&g_arena_scratch, buf);
}

static void ir_builder_init(IRBuilder *b, IRProgram *prog) {
    memset(b, 0, sizeof(IRBuilder));
    b->prog = prog;
    b->var_capacity = 256;
    b->vars = arena_calloc(&g_arena_compile, b->var_capacity, sizeof(b->vars[0]));
}

static void ir_builder_free(IRBuilder *b) {
    (void)b;
}

static void ir_add_var(IRBuilder *b, const char *name, const char *reg, bool is_global, Symbol *sym) {
    if (b->num_vars >= b->var_capacity) {
        int old_capacity = b->var_capacity;
        b->var_capacity *= 2;
        b->vars = arena_grow(&g_arena_compile, b->vars,
                             (size_t)old_capacity * sizeof(b->vars[0]),
                             (size_t)b->var_capacity * sizeof(b->vars[0]));
    }
    int i = b->num_vars++;
    b->vars[i].name = ir_strdup(&g_arena_compile, name);
    b->vars[i].reg = ir_strdup(&g_arena_compile, reg);
    b->vars[i].is_global = is_global;
    b->vars[i].symbol = sym;
}

static const char *ir_find_var(IRBuilder *b, const char *name) {
    for (int i = b->num_vars - 1; i >= 0; i--) {
        if (strcmp(b->vars[i].name, name) == 0) {
            return b->vars[i].reg;
        }
    }
    return NULL;
}

static void ir_emit(IRBuilder *b, IRInstr instr) {
    if (b->block) {
        ir_append_instr(b->block, instr);
    }
}

static IRBlock *ir_new_block_in_fn(IRBuilder *b, const char *prefix) {
    char *label = ir_gen_label(b->fn, prefix);
    IRBlock *blk = ir_add_block(b->fn, label);
    return blk;
}

// --- Expression builder ---
// Returns the virtual register containing the result

static IROperand ir_build_expr(IRBuilder *b, ASTNode *expr, SymbolTable *symbols);

// Build expression and return result as an operand
static IROperand ir_build_expr(IRBuilder *b, ASTNode *expr, SymbolTable *symbols) {
    switch (expr->type) {
        case AST_INT: {
            return op_imm(expr->data.int_value);
        }

        case AST_CHAR: {
            return op_imm(expr->data.char_value);
        }

        case AST_STRING: {
            // Create string data and return address
            char *label = ir_gen_string_label(b);
            ir_add_string(b->fn, label, expr->data.string_value);
            char *temp = ir_gen_temp(b);
            ir_emit(b, ir_instr(IR_ADDR, op_reg(temp), op_global(label), op_none(), op_none(), IR_TYPE_PTR));
            IROperand result = op_reg(temp);
            return result;
        }

        case AST_IDENTIFIER: {
            const char *var_reg = ir_find_var(b, expr->data.string_value);
            if (var_reg) {
                // Variable found in local scope - load its value
                char *temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_LOAD, op_reg(temp), op_reg(var_reg), op_none(), op_none(), IR_TYPE_I64));
                IROperand result = op_reg(temp);
                return result;
            }
            // Not found - assume it's a global or external
            char *temp = ir_gen_temp(b);

            // Check if it's a known global
            bool found_global = false;
            for (int i = 0; i < b->prog->num_globals; i++) {
                if (strcmp(b->prog->globals[i].name, expr->data.string_value) == 0) {
                    // Load from global
                    char *addr_temp = ir_gen_temp(b);
                    ir_emit(b, ir_instr(IR_ADDR, op_reg(addr_temp), op_global(expr->data.string_value), op_none(), op_none(), IR_TYPE_PTR));
                    ir_emit(b, ir_instr(IR_LOAD, op_reg(temp), op_reg(addr_temp), op_none(), op_none(), IR_TYPE_I64));
                    found_global = true;
                    break;
                }
            }

            if (!found_global) {
                // Try as a function/extern call
                fprintf(stderr, "Warning: undefined identifier '%s' in IR build\n", expr->data.string_value);
                // Emit as a global address for now
                ir_emit(b, ir_instr(IR_ADDR, op_reg(temp), op_global(expr->data.string_value), op_none(), op_none(), IR_TYPE_PTR));
            }

            IROperand result = op_reg(temp);
            return result;
        }

        case AST_LIST: {
            if (expr->data.list.count == 0) return op_none();
            ASTNode *op_node = expr->data.list.children[0];
            if (op_node->type != AST_IDENTIFIER) return op_none();
            const char *op = op_node->data.string_value;
            size_t count = expr->data.list.count;

            IROpcode binop;
            bool is_bin = false;
            if (strcmp(op, "+") == 0) { is_bin = true; binop = IR_ADD; }
            else if ((strcmp(op, "-") == 0 || strcmp(op, "*") == 0 || strcmp(op, "&") == 0 || strcmp(op, "|") == 0) && count >= 3) {
                is_bin = true;
                binop = strcmp(op, "-") == 0 ? IR_SUB : strcmp(op, "*") == 0 ? IR_MUL : strcmp(op, "&") == 0 ? IR_AND : IR_OR;
            } else if (strcmp(op, "/") == 0) { is_bin = true; binop = IR_DIV; }
            else if (strcmp(op, "%") == 0) { is_bin = true; binop = IR_MOD; }
            else if (strcmp(op, ">>") == 0) { is_bin = true; binop = IR_SHR; }
            else if (strcmp(op, "<<") == 0) { is_bin = true; binop = IR_SHL; }
            if (is_bin) {
                if (count < 3) return op_none();
                IROperand left = ir_build_expr(b, expr->data.list.children[1], symbols);
                IROperand right = ir_build_expr(b, expr->data.list.children[2], symbols);
                char *temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(binop, op_reg(temp), left, right, op_none(), IR_TYPE_I64));
                return op_reg(temp);
            }

            // Exponentiation: (** base exp) → call __clumsy_pow(base, exp)
            if (strcmp(op, "**") == 0 && count == 3) {
                IROperand base = ir_build_expr(b, expr->data.list.children[1], symbols);
                IROperand exp = ir_build_expr(b, expr->data.list.children[2], symbols);
                char *temp = ir_gen_temp(b);
                IROperand args[5] = {0};
                args[0] = op_global("__clumsy_pow");
                args[1] = base;
                args[2] = exp;
                ir_emit(b, ir_instr(IR_CALL, op_reg(temp), args[0], args[1], args[2], IR_TYPE_I64));
                IROperand result = op_reg(temp);
                return result;
            }

            // Address-of: (& x) → return alloc pointer (for locals) or IR_ADDR (for globals)
            if (strcmp(op, "&") == 0 && count == 2) {
                ASTNode *operand = expr->data.list.children[1];
                if (operand->type == AST_IDENTIFIER) {
                    const char *var_reg = ir_find_var(b, operand->data.string_value);
                    if (var_reg) {
                        // Local variable: alloc pointer IS the address
                        return op_reg(var_reg);
                    }
                    // Global variable
                    char *temp = ir_gen_temp(b);
                    ir_emit(b, ir_instr(IR_ADDR, op_reg(temp), op_global(operand->data.string_value), op_none(), op_none(), IR_TYPE_PTR));
                    return op_reg(temp);
                }
                return op_none();
            }

            // Dereference: (* ptr) → load from pointer
            if (strcmp(op, "*") == 0 && count == 2) {
                IROperand ptr = ir_build_expr(b, expr->data.list.children[1], symbols);
                char *temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_LOAD, op_reg(temp), ptr, op_none(), op_none(), IR_TYPE_I64));
                return op_reg(temp);
            }

            // Array indexing: (arr [i]) → load from base + index*8
            if (strcmp(op, "[]") == 0 && count == 3) {
                ASTNode *arr_node = expr->data.list.children[1];
                ASTNode *idx_node = expr->data.list.children[2];

                // Get base pointer
                IROperand base;
                if (arr_node->type == AST_IDENTIFIER) {
                    const char *var_reg = ir_find_var(b, arr_node->data.string_value);
                    if (var_reg) {
                        base = op_reg(var_reg);
                    } else {
                        // Global array
                        char *addr_temp = ir_gen_temp(b);
                        ir_emit(b, ir_instr(IR_ADDR, op_reg(addr_temp), op_global(arr_node->data.string_value), op_none(), op_none(), IR_TYPE_PTR));
                        base = op_reg(addr_temp);
                    }
                } else {
                    base = ir_build_expr(b, arr_node, symbols);
                }

                IROperand idx = ir_build_expr(b, idx_node, symbols);

                // offset = idx * 8
                char *offset_temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_MUL, op_reg(offset_temp), idx, op_imm(8), op_none(), IR_TYPE_I64));

                // ptr = base + offset
                char *ptr_temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_ADD, op_reg(ptr_temp), base, op_reg(offset_temp), op_none(), IR_TYPE_PTR));

                // Load value
                char *temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_LOAD, op_reg(temp), op_reg(ptr_temp), op_none(), op_none(), IR_TYPE_I64));
                return op_reg(temp);
            }

            // Field access: (p.x) → load from base + field_offset
            if (strcmp(op, ".") == 0 && count == 3) {
                ASTNode *struct_node = expr->data.list.children[1];
                ASTNode *field_node = expr->data.list.children[2];

                if (struct_node->type == AST_IDENTIFIER && field_node->type == AST_IDENTIFIER) {
                    const char *struct_name = struct_node->data.string_value;
                    const char *field_name = field_node->data.string_value;

                    // Get struct base pointer
                    IROperand base;
                    const char *var_reg = ir_find_var(b, struct_name);
                    if (var_reg) {
                        base = op_reg(var_reg);
                    } else {
                        char *addr_temp = ir_gen_temp(b);
                        ir_emit(b, ir_instr(IR_ADDR, op_reg(addr_temp), op_global(struct_name), op_none(), op_none(), IR_TYPE_PTR));
                        base = op_reg(addr_temp);
                    }

                    // Compute field offset by looking up the struct type
                    // For now, look up field name in known struct fields
                    int field_offset = 0;
                    bool found = false;

                    // Check all struct definitions in the program
                    for (int si = 0; si < b->prog->num_globals; si++) {
                        if (b->prog->globals[si].struct_fields) {
                            for (int fi = 0; fi < b->prog->globals[si].num_struct_fields; fi++) {
                                if (strcmp(b->prog->globals[si].struct_fields[fi], field_name) == 0) {
                                    field_offset = fi * 8; // each field is 8 bytes
                                    found = true;
                                    break;
                                }
                            }
                            if (found) break;
                        }
                    }

                    // Load the struct pointer from the variable slot first
                    char *struct_ptr = ir_gen_temp(b);
                    ir_emit(b, ir_instr(IR_LOAD, op_reg(struct_ptr), base, op_none(), op_none(), IR_TYPE_PTR));

                    // ptr = struct_ptr + field_offset
                    char *ptr_temp = ir_gen_temp(b);
                    ir_emit(b, ir_instr(IR_ADD, op_reg(ptr_temp), op_reg(struct_ptr), op_imm(field_offset), op_none(), IR_TYPE_PTR));

                    // Load value
                    char *temp = ir_gen_temp(b);
                    ir_emit(b, ir_instr(IR_LOAD, op_reg(temp), op_reg(ptr_temp), op_none(), op_none(), IR_TYPE_I64));
                    return op_reg(temp);
                }
                return op_none();
            }

            // Struct literal: (#(val1 val2 ...)) → alloc + store fields
            if (strcmp(op, "#") == 0 && count == 2) {
                ASTNode *fields_node = expr->data.list.children[1];
                if (fields_node->type == AST_LIST) {
                    int num_fields = fields_node->data.list.count;
                    int struct_size = num_fields * 8; // each field is 8 bytes

                    // Allocate struct
                    char *alloc_temp = ir_gen_temp(b);
                    ir_emit(b, ir_instr(IR_ALLOC, op_reg(alloc_temp), op_imm(struct_size), op_none(), op_none(), IR_TYPE_PTR));

                    // Store each field
                    for (int fi = 0; fi < num_fields; fi++) {
                        ASTNode *field_val = fields_node->data.list.children[fi];
                        IROperand val = ir_build_expr(b, field_val, symbols);

                        char *offset_temp = ir_gen_temp(b);
                        ir_emit(b, ir_instr(IR_ADD, op_reg(offset_temp), op_reg(alloc_temp), op_imm(fi * 8), op_none(), IR_TYPE_PTR));

                        ir_emit(b, ir_instr(IR_STORE, op_none(), val, op_reg(offset_temp), op_none(), IR_TYPE_VOID));
                    }

                    return op_reg(alloc_temp);
                }
                return op_none();
            }

            // Comparisons
            if (strcmp(op, "=") == 0 || strcmp(op, "!=") == 0 ||
                strcmp(op, "<") == 0 || strcmp(op, ">") == 0 ||
                strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0) {

                if (expr->data.list.count < 3) return op_none();
                IROperand left = ir_build_expr(b, expr->data.list.children[1], symbols);
                IROperand right = ir_build_expr(b, expr->data.list.children[2], symbols);

                IROpcode opcode;
                if (strcmp(op, "=") == 0) opcode = IR_EQ;
                else if (strcmp(op, "!=") == 0) opcode = IR_NE;
                else if (strcmp(op, "<") == 0) opcode = IR_LT;
                else if (strcmp(op, ">") == 0) opcode = IR_GT;
                else if (strcmp(op, "<=") == 0) opcode = IR_LE;
                else if (strcmp(op, ">=") == 0) opcode = IR_GE;
                else return op_none();

                char *temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(opcode, op_reg(temp), left, right, op_none(), IR_TYPE_I64));
                IROperand result = op_reg(temp);
                return result;
            }

            // Logical operators
            if (strcmp(op, "&&") == 0) {
                // Short-circuit AND — returns 0 or 1
                if (expr->data.list.count < 3) return op_none();

                IRBlock *end_blk = ir_new_block_in_fn(b, "and_end");
                IRBlock *false_blk = ir_new_block_in_fn(b, "and_false");
                IRBlock *right_blk = ir_new_block_in_fn(b, "and_right");

                // Evaluate left
                IROperand left = ir_build_expr(b, expr->data.list.children[1], symbols);
                char *left_temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_MOV, op_reg(left_temp), left, op_none(), op_none(), IR_TYPE_I64));

                // If left is true, evaluate right; if false, result is 0
                ir_emit(b, ir_instr(IR_JNZ, op_none(), op_reg(left_temp), op_block(right_blk->label), op_block(false_blk->label), IR_TYPE_VOID));

                // Right side in new block — booleanize result
                b->block = right_blk;
                IROperand right = ir_build_expr(b, expr->data.list.children[2], symbols);
                char *right_raw = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_MOV, op_reg(right_raw), right, op_none(), op_none(), IR_TYPE_I64));
                // right_bool = (right != 0)
                char *right_temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_NE, op_reg(right_temp), op_reg(right_raw), op_imm(0), op_none(), IR_TYPE_I64));
                // Track actual block that jumps to end_blk (ir_build_expr may have changed b->block)
                IRBlock *right_end_blk = b->block;
                ir_emit(b, ir_instr(IR_JMP, op_none(), op_block(end_blk->label), op_none(), op_none(), IR_TYPE_VOID));

                // False block: result is 0
                b->block = false_blk;
                char *false_temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_MOV, op_reg(false_temp), op_imm(0), op_none(), op_none(), IR_TYPE_I64));
                ir_emit(b, ir_instr(IR_JMP, op_none(), op_block(end_blk->label), op_none(), op_none(), IR_TYPE_VOID));

                // End block: phi
                b->block = end_blk;
                char *phi_temp = ir_gen_temp(b);
                IROperand pairs[4] = {op_reg(right_temp), op_block(right_end_blk->label),
                                       op_reg(false_temp), op_block(false_blk->label)};
                ir_emit(b, ir_phi(op_reg(phi_temp), pairs, 2, IR_TYPE_I64));

                return op_reg(phi_temp);
            }

            if (strcmp(op, "||") == 0) {
                // Short-circuit OR — returns 0 or 1
                if (expr->data.list.count < 3) return op_none();

                IRBlock *end_blk = ir_new_block_in_fn(b, "or_end");
                IRBlock *true_blk = ir_new_block_in_fn(b, "or_true");
                IRBlock *right_blk = ir_new_block_in_fn(b, "or_right");

                // Evaluate left
                IROperand left = ir_build_expr(b, expr->data.list.children[1], symbols);
                char *left_temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_MOV, op_reg(left_temp), left, op_none(), op_none(), IR_TYPE_I64));

                // If left is true, result is 1; if false, evaluate right
                ir_emit(b, ir_instr(IR_JNZ, op_none(), op_reg(left_temp), op_block(true_blk->label), op_block(right_blk->label), IR_TYPE_VOID));

                // Right side in new block — booleanize result
                b->block = right_blk;
                IROperand right = ir_build_expr(b, expr->data.list.children[2], symbols);
                char *right_raw = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_MOV, op_reg(right_raw), right, op_none(), op_none(), IR_TYPE_I64));
                // right_bool = (right != 0)
                char *right_temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_NE, op_reg(right_temp), op_reg(right_raw), op_imm(0), op_none(), IR_TYPE_I64));
                // Track actual block that jumps to end_blk (ir_build_expr may have changed b->block)
                IRBlock *right_end_blk = b->block;
                ir_emit(b, ir_instr(IR_JMP, op_none(), op_block(end_blk->label), op_none(), op_none(), IR_TYPE_VOID));

                // True block: result is 1
                b->block = true_blk;
                char *true_temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_MOV, op_reg(true_temp), op_imm(1), op_none(), op_none(), IR_TYPE_I64));
                ir_emit(b, ir_instr(IR_JMP, op_none(), op_block(end_blk->label), op_none(), op_none(), IR_TYPE_VOID));

                // End block: phi
                b->block = end_blk;
                char *phi_temp = ir_gen_temp(b);
                IROperand pairs[4] = {op_reg(right_temp), op_block(right_end_blk->label),
                                       op_reg(true_temp), op_block(true_blk->label)};
                ir_emit(b, ir_phi(op_reg(phi_temp), pairs, 2, IR_TYPE_I64));

                return op_reg(phi_temp);
            }

            // Not / !
            if (strcmp(op, "not") == 0 || strcmp(op, "!") == 0) {
                if (expr->data.list.count < 2) return op_none();
                IROperand val = ir_build_expr(b, expr->data.list.children[1], symbols);
                char *temp = ir_gen_temp(b);
                // not x = (x == 0) ? 1 : 0
                ir_emit(b, ir_instr(IR_EQ, op_reg(temp), val, op_imm(0), op_none(), IR_TYPE_I64));
                IROperand result = op_reg(temp);
                return result;
            }

            // Bitwise NOT ~
            if (strcmp(op, "~") == 0) {
                if (expr->data.list.count < 2) return op_none();
                IROperand val = ir_build_expr(b, expr->data.list.children[1], symbols);
                char *temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_NOT, op_reg(temp), val, op_none(), op_none(), IR_TYPE_I64));
                IROperand result = op_reg(temp);
                return result;
            }

            // Negation -x
            if (strcmp(op, "-") == 0 && expr->data.list.count == 2) {
                IROperand val = ir_build_expr(b, expr->data.list.children[1], symbols);
                char *temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_NEG, op_reg(temp), val, op_none(), op_none(), IR_TYPE_I64));
                IROperand result = op_reg(temp);
                return result;
            }

            // Function call
            // Check if first child is an identifier that's a known function/extern
            if (op_node->type == AST_IDENTIFIER) {
                const char *func_name = op_node->data.string_value;

                // Build arguments
                IROperand args[5] = {0};
                args[0] = op_global(func_name);  // function name
                int num_args = 0;

                for (size_t i = 1; i < expr->data.list.count && num_args < 7; i++) {
                    args[num_args + 1] = ir_build_expr(b, expr->data.list.children[i], symbols);
                    num_args++;
                }

                // Check if this is a void function (like println)
                bool is_void = false;
                // Look up in externs
                for (int i = 0; i < b->prog->num_externs; i++) {
                    if (strcmp(b->prog->externs[i].name, func_name) == 0) {
                        is_void = (b->prog->externs[i].return_type == IR_TYPE_VOID);
                        break;
                    }
                }

                if (is_void) {
                    ir_emit(b, ir_call(op_none(), args, num_args + 1, IR_TYPE_VOID));
                    return op_none();
                } else {
                    char *temp = ir_gen_temp(b);
                    ir_emit(b, ir_call(op_reg(temp), args, num_args + 1, IR_TYPE_I64));
                    IROperand result = op_reg(temp);
                    return result;
                }
            }

            return op_none();
        }

        case AST_FN: {
            // Function literal - not handled here, should be at top level
            return op_none();
        }

        case AST_ARRAY: {
            // Array literal - not handled in expression context
            return op_none();
        }

        case AST_EXTERN_DECL: {
            return op_none();
        }


        default:
            return op_none();
    }
}


static void ir_add_global_from_init(IRBuilder *b, const char *name, ASTNode *init_value, ASTNode *type_node, bool is_public) {
    if (!init_value) {
        ir_add_global(b->prog, name, 0, false, NULL, 0, is_public);
        return;
    }
    if (init_value->type == AST_INT) {
        ir_add_global(b->prog, name, init_value->data.int_value, false, NULL, 0, is_public);
    } else if (init_value->type == AST_STRING) {
        ir_add_global(b->prog, name, 0, true, init_value->data.string_value, 0, is_public);
    } else if (init_value->type == AST_ARRAY) {
        int arr_size = init_value->data.list.count;
        if (type_node && type_node->type == AST_LIST && type_node->data.list.count >= 3) {
            ASTNode *size_node = type_node->data.list.children[2];
            if (size_node->type == AST_INT && size_node->data.int_value > arr_size)
                arr_size = size_node->data.int_value;
        }
        int64_t *vals = arena_calloc(&g_arena_scratch, arr_size, sizeof(int64_t));
        for (int ai = 0; ai < (int)init_value->data.list.count; ai++) {
            if (init_value->data.list.children[ai]->type == AST_INT)
                vals[ai] = init_value->data.list.children[ai]->data.int_value;
        }
        ir_add_global_array(b->prog, name, vals, arr_size, is_public);
    } else {
        ir_add_global(b->prog, name, 0, false, NULL, 0, is_public);
    }
}
// --- Statement builder ---

// Register an extern declaration (function or variable) into the IR program.
// Used for top-level and function-scoped (extern ...) forms. A function extern
// is unambiguous only when a return type is present (count == 3) and the param
// list is an AST_LIST; a variable extern always has count == 2 (name + type),
// though its type node (e.g. "int[4]" or a pointer "(* void)") may also be an
// AST_LIST.
static void ir_build_extern_decl(IRBuilder *b, ASTNode *stmt) {
    if (!stmt || stmt->type != AST_EXTERN_DECL) return;

    const char *name = stmt->data.list.children[0]->data.string_value;

    if (stmt->data.list.count >= 3 && stmt->data.list.children[1]->type == AST_LIST) {
        ASTNode *params_node = stmt->data.list.children[1];
        bool is_index_access = false;
        if (params_node->data.list.count > 0 &&
            params_node->data.list.children[0]->type == AST_IDENTIFIER &&
            strcmp(params_node->data.list.children[0]->data.string_value, "[]") == 0) {
            is_index_access = true;
        }
        if (!is_index_access) {
            IRType ret_type = IR_TYPE_I64;

            if (stmt->data.list.count >= 3) {
                ASTNode *ret_node = stmt->data.list.children[2];
                if (ret_node->type == AST_IDENTIFIER) {
                    if (strcmp(ret_node->data.string_value, "void") == 0) ret_type = IR_TYPE_VOID;
                    else if (strcmp(ret_node->data.string_value, "int") == 0) ret_type = IR_TYPE_I64;
                    else if (ret_node->data.string_value[0] == '*') ret_type = IR_TYPE_PTR;
                }
            }

            int num_params = params_node->data.list.count;
            IRType *param_types = arena_calloc(&g_arena_scratch, num_params, sizeof(IRType));
            for (int j = 0; j < num_params; j++) {
                ASTNode *pt = params_node->data.list.children[j];
                if (pt->type == AST_IDENTIFIER) {
                    if (strcmp(pt->data.string_value, "void") == 0) param_types[j] = IR_TYPE_VOID;
                    else if (strcmp(pt->data.string_value, "int") == 0) param_types[j] = IR_TYPE_I64;
                    else if (pt->data.string_value[0] == '*') param_types[j] = IR_TYPE_PTR;
                    else param_types[j] = IR_TYPE_I64;
                } else {
                    param_types[j] = IR_TYPE_I64;
                }
            }

            bool is_variadic = false;
            if (num_params > 0 && params_node->data.list.children[num_params - 1]->type == AST_IDENTIFIER &&
                strcmp(params_node->data.list.children[num_params - 1]->data.string_value, "...") == 0) {
                is_variadic = true;
                num_params--;
            }

            ir_add_extern(b->prog, name, ret_type, param_types, num_params, is_variadic);
            return;
        }
    }

    // Variable extern: (extern name type) — type is a plain identifier
    // ("int", "*void") or an index-access node ("int[4]"). Add as a
    // global but mark as extern so the emitter doesn't emit data.
    ir_add_global(b->prog, name, 0, false, NULL, 0, false);
    b->prog->globals[b->prog->num_globals - 1].is_extern = true;
}

static void ir_build_stmt(IRBuilder *b, ASTNode *stmt, SymbolTable *symbols);

static void ir_build_stmt(IRBuilder *b, ASTNode *stmt, SymbolTable *symbols) {
    if (!stmt) return;

    // Function-scoped extern declaration: register it like a top-level one.
    // (expression position also accepts AST_EXTERN_DECL as a no-op)
    if (stmt->type == AST_EXTERN_DECL) {
        ir_build_extern_decl(b, stmt);
        return;
    }

    // Handle bare expressions (function calls as statements)
    if (stmt->type == AST_LIST && stmt->data.list.count > 0) {
        ASTNode *op_node = stmt->data.list.children[0];
        if (op_node->type == AST_IDENTIFIER) {
            const char *op = op_node->data.string_value;

            // Let statement
            if (strcmp(op, "let") == 0) {
                if (stmt->data.list.count >= 3) {
                    ASTNode *name_node = stmt->data.list.children[1];
                    const char *name = name_node->data.string_value;

                    // Check if this is a function definition
                    ASTNode *init_value = NULL;
                    ASTNode *type_node = NULL;
                    if (stmt->data.list.count == 3) {
                        init_value = stmt->data.list.children[2];
                    } else if (stmt->data.list.count >= 4) {
                        type_node = stmt->data.list.children[2];
                        init_value = stmt->data.list.children[3];
                    }

                    if (init_value && init_value->type == AST_FN) {
                        // Function definition - handled at top level
                        return;
                    }

                    // Check for struct type definition: (let Name struct #((field type val)...))
                    if (type_node && type_node->type == AST_IDENTIFIER &&
                        strcmp(type_node->data.string_value, "struct") == 0 &&
                        init_value && init_value->type == AST_LIST) {
                        // Extract struct field names from #((field type val) ...) literal
                        ASTNode *struct_lit = init_value;
                        if (struct_lit->data.list.count >= 2) {
                            ASTNode *fields_container = struct_lit->data.list.children[1];
                            if (fields_container->type == AST_LIST) {
                                int num_fields = fields_container->data.list.count;
                                char **field_names = arena_calloc(&g_arena_scratch, num_fields, sizeof(char *));
                                for (int fi = 0; fi < num_fields; fi++) {
                                    ASTNode *field_decl = fields_container->data.list.children[fi];
                                    if (field_decl->type == AST_LIST && field_decl->data.list.count >= 2) {
                                        field_names[fi] = arena_strdup(&g_arena_scratch, field_decl->data.list.children[0]->data.string_value);
                                    } else {
                                        field_names[fi] = arena_strdup(&g_arena_scratch, "_field");
                                    }
                                }
                                ir_add_struct_global(b->prog, name, field_names, num_fields, false);
                            }
                        }
                        return;
                    }

                    // Determine allocation size
                    int alloc_bytes = 16; // default scalar
                    bool is_array = false;
                    int array_count = 0;
                    if (type_node) {
                        // Check for array type: ([] int N) or (array_type int N)
                        if (type_node->type == AST_LIST && type_node->data.list.count >= 3) {
                            ASTNode *type_op = type_node->data.list.children[0];
                            if (type_op->type == AST_IDENTIFIER &&
                                (strcmp(type_op->data.string_value, "array_type") == 0 ||
                                 strcmp(type_op->data.string_value, "[]") == 0)) {
                                ASTNode *size_node = type_node->data.list.children[2];
                                if (size_node->type == AST_INT) {
                                    array_count = size_node->data.int_value;
                                    alloc_bytes = array_count * 8;
                                    is_array = true;
                                }
                            }
                        }
                        // Check for struct instance: (let p Point #(2 3))
                        else if (type_node->type == AST_IDENTIFIER && init_value &&
                                 init_value->type == AST_LIST) {
                            ASTNode *init_op = init_value->data.list.children[0];
                            if (init_op->type == AST_IDENTIFIER && strcmp(init_op->data.string_value, "#") == 0) {
                                // Struct literal - use the struct's field count
                                for (int si = 0; si < b->prog->num_globals; si++) {
                                    if (b->prog->globals[si].is_struct &&
                                        strcmp(b->prog->globals[si].name, type_node->data.string_value) == 0) {
                                        alloc_bytes = b->prog->globals[si].num_struct_fields * 8;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    // Allocate stack space
                    char *ptr_reg = ir_gen_temp(b);
                    ir_emit(b, ir_instr(IR_ALLOC, op_reg(ptr_reg), op_imm(alloc_bytes), op_none(), op_none(), IR_TYPE_PTR));

                    // Initialize if value provided
                    if (init_value) {
                        if (is_array && init_value->type == AST_ARRAY) {
                            // Array initialization: store each element
                            for (int ai = 0; ai < array_count && ai < (int)init_value->data.list.count; ai++) {
                                IROperand val = ir_build_expr(b, init_value->data.list.children[ai], symbols);
                                char *offset_temp = ir_gen_temp(b);
                                ir_emit(b, ir_instr(IR_MUL, op_reg(offset_temp), op_imm(ai), op_imm(8), op_none(), IR_TYPE_I64));
                                char *ptr_temp = ir_gen_temp(b);
                                ir_emit(b, ir_instr(IR_ADD, op_reg(ptr_temp), op_reg(ptr_reg), op_reg(offset_temp), op_none(), IR_TYPE_PTR));
                                ir_emit(b, ir_instr(IR_STORE, op_none(), val, op_reg(ptr_temp), op_none(), IR_TYPE_VOID));
                            }
                        } else {
                            IROperand val = ir_build_expr(b, init_value, symbols);
                            ir_emit(b, ir_instr(IR_STORE, op_none(), val, op_reg(ptr_reg), op_none(), IR_TYPE_VOID));
                        }
                    }

                    ir_add_var(b, name, ptr_reg, false, NULL);
                }
                return;
            }

            // Set statement
            if (strcmp(op, "set") == 0) {
                if (stmt->data.list.count >= 3) {
                    ASTNode *target = stmt->data.list.children[1];
                    IROperand val = ir_build_expr(b, stmt->data.list.children[2], symbols);

                    if (target->type == AST_IDENTIFIER) {
                        const char *var_reg = ir_find_var(b, target->data.string_value);
                        if (var_reg) {
                            ir_emit(b, ir_instr(IR_STORE, op_none(), val, op_reg(var_reg), op_none(), IR_TYPE_VOID));
                        } else {
                            // Global variable
                            char *addr_temp = ir_gen_temp(b);
                            ir_emit(b, ir_instr(IR_ADDR, op_reg(addr_temp), op_global(target->data.string_value), op_none(), op_none(), IR_TYPE_PTR));
                            ir_emit(b, ir_instr(IR_STORE, op_none(), val, op_reg(addr_temp), op_none(), IR_TYPE_VOID));
                        }
                    } else if (target->type == AST_LIST && target->data.list.count >= 3) {
                        // Array element set: (set [] arr i) → (set arr[i] val)
                        ASTNode *op_child = target->data.list.children[0];
                        if (op_child->type == AST_IDENTIFIER && strcmp(op_child->data.string_value, "[]") == 0) {
                            ASTNode *arr_node = target->data.list.children[1];
                            ASTNode *idx_node = target->data.list.children[2];

                            if (arr_node->type == AST_IDENTIFIER) {
                                const char *arr_name = arr_node->data.string_value;
                                const char *var_reg = ir_find_var(b, arr_name);

                                IROperand idx = ir_build_expr(b, idx_node, symbols);
                                IROperand base;

                                if (var_reg) {
                                    base = op_reg(var_reg);
                                } else {
                                    char *addr_temp = ir_gen_temp(b);
                                    ir_emit(b, ir_instr(IR_ADDR, op_reg(addr_temp), op_global(arr_name), op_none(), op_none(), IR_TYPE_PTR));
                                    base = op_reg(addr_temp);
                                }

                                // offset = idx * 8
                                char *offset_temp = ir_gen_temp(b);
                                ir_emit(b, ir_instr(IR_MUL, op_reg(offset_temp), idx, op_imm(8), op_none(), IR_TYPE_I64));

                                // ptr = base + offset
                                char *ptr_temp = ir_gen_temp(b);
                                ir_emit(b, ir_instr(IR_ADD, op_reg(ptr_temp), base, op_reg(offset_temp), op_none(), IR_TYPE_PTR));

                                ir_emit(b, ir_instr(IR_STORE, op_none(), val, op_reg(ptr_temp), op_none(), IR_TYPE_VOID));
                            }
                        } else if (op_child->type == AST_IDENTIFIER && strcmp(op_child->data.string_value, ".") == 0) {
                            // Struct field set: (set . p x) → (set p.x val)
                            ASTNode *struct_node = target->data.list.children[1];
                            ASTNode *field_node = target->data.list.children[2];

                            if (struct_node->type == AST_IDENTIFIER && field_node->type == AST_IDENTIFIER) {
                                const char *struct_name = struct_node->data.string_value;
                                const char *field_name = field_node->data.string_value;

                                IROperand base;
                                const char *var_reg = ir_find_var(b, struct_name);
                                if (var_reg) {
                                    base = op_reg(var_reg);
                                } else {
                                    char *addr_temp = ir_gen_temp(b);
                                    ir_emit(b, ir_instr(IR_ADDR, op_reg(addr_temp), op_global(struct_name), op_none(), op_none(), IR_TYPE_PTR));
                                    base = op_reg(addr_temp);
                                }

                                // Compute field offset
                                int field_offset = 0;
                                for (int si = 0; si < b->prog->num_globals; si++) {
                                    if (b->prog->globals[si].struct_fields) {
                                        for (int fi = 0; fi < b->prog->globals[si].num_struct_fields; fi++) {
                                            if (strcmp(b->prog->globals[si].struct_fields[fi], field_name) == 0) {
                                                field_offset = fi * 8;
                                                break;
                                            }
                                        }
                                    }
                                }

                                char *ptr_temp = ir_gen_temp(b);
                                ir_emit(b, ir_instr(IR_ADD, op_reg(ptr_temp), base, op_imm(field_offset), op_none(), IR_TYPE_PTR));
                                ir_emit(b, ir_instr(IR_STORE, op_none(), val, op_reg(ptr_temp), op_none(), IR_TYPE_VOID));
                            }
                        }
                    }
                }
                return;
            }

            // If statement
            if (strcmp(op, "if") == 0) {
                if (stmt->data.list.count < 3) return;

                IROperand cond = ir_build_expr(b, stmt->data.list.children[1], symbols);

                // Ensure cond is in a temp
                char *cond_temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_MOV, op_reg(cond_temp), cond, op_none(), op_none(), IR_TYPE_I64));

                IRBlock *then_blk = ir_new_block_in_fn(b, "if_then");
                IRBlock *else_blk = ir_new_block_in_fn(b, "if_else");
                IRBlock *merge_blk = ir_new_block_in_fn(b, "if_merge");

                // Branch
                if (stmt->data.list.count > 3) {
                    ir_emit(b, ir_instr(IR_JNZ, op_none(), op_reg(cond_temp), op_block(then_blk->label), op_block(else_blk->label), IR_TYPE_VOID));
                } else {
                    ir_emit(b, ir_instr(IR_JNZ, op_none(), op_reg(cond_temp), op_block(then_blk->label), op_block(merge_blk->label), IR_TYPE_VOID));
                }

                // Then block
                b->block = then_blk;
                ir_build_stmt(b, stmt->data.list.children[2], symbols);
                if ((b->block && b->block->num_instrs == 0) ||
                    (b->block && b->block->num_instrs > 0 && b->block->instrs[b->block->num_instrs - 1].op != IR_JMP &&
                     b->block->instrs[b->block->num_instrs - 1].op != IR_RET &&
                     b->block->instrs[b->block->num_instrs - 1].op != IR_JNZ)) {
                    ir_emit(b, ir_instr(IR_JMP, op_none(), op_block(merge_blk->label), op_none(), op_none(), IR_TYPE_VOID));
                }

                // Else block
                if (stmt->data.list.count > 3) {
                    b->block = else_blk;
                    ir_build_stmt(b, stmt->data.list.children[3], symbols);
                    if ((b->block && b->block->num_instrs == 0) ||
                        (b->block && b->block->num_instrs > 0 && b->block->instrs[b->block->num_instrs - 1].op != IR_JMP &&
                         b->block->instrs[b->block->num_instrs - 1].op != IR_RET &&
                         b->block->instrs[b->block->num_instrs - 1].op != IR_JNZ)) {
                        ir_emit(b, ir_instr(IR_JMP, op_none(), op_block(merge_blk->label), op_none(), op_none(), IR_TYPE_VOID));
                    }
                } else {
                    // No else: jump from else_blk to merge
                    b->block = else_blk;
                    ir_emit(b, ir_instr(IR_JMP, op_none(), op_block(merge_blk->label), op_none(), op_none(), IR_TYPE_VOID));
                }

                // Continue in merge block
                b->block = merge_blk;
                return;
            }

            // While statement
            if (strcmp(op, "while") == 0) {
                if (stmt->data.list.count < 3) return;

                IRBlock *cond_blk = ir_new_block_in_fn(b, "while_cond");
                IRBlock *body_blk = ir_new_block_in_fn(b, "while_body");
                IRBlock *exit_blk = ir_new_block_in_fn(b, "while_exit");

                // Jump to condition
                ir_emit(b, ir_instr(IR_JMP, op_none(), op_block(cond_blk->label), op_none(), op_none(), IR_TYPE_VOID));

                // Condition block
                b->block = cond_blk;
                IROperand cond = ir_build_expr(b, stmt->data.list.children[1], symbols);
                char *cond_temp = ir_gen_temp(b);
                ir_emit(b, ir_instr(IR_MOV, op_reg(cond_temp), cond, op_none(), op_none(), IR_TYPE_I64));
                ir_emit(b, ir_instr(IR_JNZ, op_none(), op_reg(cond_temp), op_block(body_blk->label), op_block(exit_blk->label), IR_TYPE_VOID));

                // Body block
                b->block = body_blk;

                // Push loop context
                b->loop_stack[b->loop_depth].break_label = exit_blk->label;
                b->loop_stack[b->loop_depth].continue_label = cond_blk->label;
                b->loop_depth++;

                ir_build_stmt(b, stmt->data.list.children[2], symbols);

                b->loop_depth--;

                // Jump back to condition (if body didn't end with ret/break)
                if ((b->block && b->block->num_instrs == 0) ||
                    (b->block && b->block->num_instrs > 0 && b->block->instrs[b->block->num_instrs - 1].op != IR_JMP &&
                     b->block->instrs[b->block->num_instrs - 1].op != IR_RET &&
                     b->block->instrs[b->block->num_instrs - 1].op != IR_JNZ)) {
                    ir_emit(b, ir_instr(IR_JMP, op_none(), op_block(cond_blk->label), op_none(), op_none(), IR_TYPE_VOID));
                }

                // Continue in exit block
                b->block = exit_blk;
                return;
            }

            // Begin block
            if (strcmp(op, "begin") == 0) {
                for (size_t i = 1; i < stmt->data.list.count; i++) {
                    ir_build_stmt(b, stmt->data.list.children[i], symbols);
                }
                return;
            }

            // Return
            if (strcmp(op, "ret") == 0) {
                if (stmt->data.list.count >= 2) {
                    IROperand val = ir_build_expr(b, stmt->data.list.children[1], symbols);
                    ir_emit(b, ir_instr(IR_RET, op_none(), val, op_none(), op_none(), IR_TYPE_VOID));
                } else {
                    ir_emit(b, ir_instr(IR_RET, op_none(), op_none(), op_none(), op_none(), IR_TYPE_VOID));
                }
                b->block = NULL;  // Dead code after ret
                return;
            }

            // Break
            if (strcmp(op, "break") == 0) {
                if (b->loop_depth > 0) {
                    ir_emit(b, ir_instr(IR_JMP, op_none(), op_block(b->loop_stack[b->loop_depth - 1].break_label), op_none(), op_none(), IR_TYPE_VOID));
                    b->block = NULL;  // Dead code after break
                }
                return;
            }

            // Continue
            if (strcmp(op, "continue") == 0) {
                if (b->loop_depth > 0) {
                    ir_emit(b, ir_instr(IR_JMP, op_none(), op_block(b->loop_stack[b->loop_depth - 1].continue_label), op_none(), op_none(), IR_TYPE_VOID));
                    b->block = NULL;  // Dead code after continue
                }
                return;
            }

            // Print built-in: (print expr) → printf("%ld", expr) or printf("%s", expr)
            if (strcmp(op, "print") == 0) {
                if (stmt->data.list.count >= 2) {
                    ASTNode *expr = stmt->data.list.children[1];

                    // Determine format string
                    const char *fmt;
                    if (expr->type == AST_STRING) {
                        fmt = "%s";
                    } else if (expr->type == AST_CHAR) {
                        fmt = "%c";
                    } else {
                        fmt = "%ld";
                    }

                    // Create format string label
                    char *fmt_label = ir_gen_string_label(b);
                    ir_add_string(b->fn, fmt_label, fmt);

                    // Build expression argument
                    IROperand val = ir_build_expr(b, expr, symbols);

                    // Call printf(fmt, val)
                    IROperand args[3] = {op_global("printf"), op_global(fmt_label), val};
                    IRInstr call = ir_instr(IR_CALL, op_none(), args[0], args[1], args[2], IR_TYPE_VOID);
                    call.is_variadic = true;
                    ir_emit(b, call);
                }
                return;
            }

            if (strcmp(op, "pub") == 0) {
                if (stmt->data.list.count >= 3) {
                    ASTNode *name_node = stmt->data.list.children[1];
                    ASTNode *init_value = stmt->data.list.count == 3 ? stmt->data.list.children[2]
                                           : stmt->data.list.count >= 4 ? stmt->data.list.children[3] : NULL;
                    if (init_value && init_value->type == AST_FN) return;
                    ASTNode *type_node = stmt->data.list.count >= 4 ? stmt->data.list.children[2] : NULL;
                    ir_add_global_from_init(b, name_node->data.string_value, init_value, type_node, true);
                }
                return;
            }
        }

        // Bare function call
        IROperand result = ir_build_expr(b, stmt, symbols);
        // Discard result
        (void)result;
        return;
    }

    // Handle AST_FN at top level (shouldn't happen here)
}

// --- Top-level builder ---

static void ir_build_top_level(IRBuilder *b, ASTNode *ast, SymbolTable *symbols) {
    if (!ast || ast->type != AST_LIST) return;

    for (size_t i = 0; i < ast->data.list.count; i++) {
        ASTNode *stmt = ast->data.list.children[i];

        // Extern declaration (separate AST node type from AST_LIST)
        if (stmt->type == AST_EXTERN_DECL) {
            ir_build_extern_decl(b, stmt);
            continue;
        }

        if (stmt->type == AST_LIST && stmt->data.list.count > 0) {
            ASTNode *op_node = stmt->data.list.children[0];
            if (op_node->type == AST_IDENTIFIER) {
                const char *op = op_node->data.string_value;

                // Let/Pub statement
                if (strcmp(op, "let") == 0 || strcmp(op, "pub") == 0) {
                    if (stmt->data.list.count >= 3) {
                        ASTNode *name_node = stmt->data.list.children[1];
                        const char *name = name_node->data.string_value;

                        ASTNode *init_value = NULL;
                        if (stmt->data.list.count == 3) {
                            init_value = stmt->data.list.children[2];
                        } else if (stmt->data.list.count >= 4) {
                            init_value = stmt->data.list.children[3];
                        }

                        bool is_pub = (strcmp(op, "pub") == 0);

                        // Check for struct type definition: (let Name struct #((field type val)...))
                        if (stmt->data.list.count >= 4) {
                            ASTNode *type_node_check = stmt->data.list.children[2];
                            if (type_node_check->type == AST_IDENTIFIER &&
                                strcmp(type_node_check->data.string_value, "struct") == 0 &&
                                init_value && init_value->type == AST_LIST) {
                                ASTNode *struct_lit = init_value;
                                if (struct_lit->data.list.count >= 2) {
                                    ASTNode *fields_container = struct_lit->data.list.children[1];
                                    if (fields_container->type == AST_LIST) {
                                        int num_fields = fields_container->data.list.count;
                                        char **field_names = arena_calloc(&g_arena_scratch, num_fields, sizeof(char *));
                                        for (int fi = 0; fi < num_fields; fi++) {
                                            ASTNode *field_decl = fields_container->data.list.children[fi];
                                            if (field_decl->type == AST_LIST && field_decl->data.list.count >= 2) {
                                                field_names[fi] = arena_strdup(&g_arena_scratch, field_decl->data.list.children[0]->data.string_value);
                                            } else {
                                                field_names[fi] = arena_strdup(&g_arena_scratch, "_field");
                                            }
                                        }
                                        ir_add_struct_global(b->prog, name, field_names, num_fields, is_pub);
                                        continue;
                                    }
                                }
                            }
                        }

                        if (init_value && init_value->type == AST_FN) {
                            // Function definition
                            IRType ret_type = IR_TYPE_I64;
                            ASTNode *ret_type_node = init_value->data.fn.return_type;
                            if (ret_type_node && ret_type_node->type == AST_IDENTIFIER) {
                                if (strcmp(ret_type_node->data.string_value, "void") == 0) ret_type = IR_TYPE_VOID;
                                else if (strcmp(ret_type_node->data.string_value, "int") == 0) ret_type = IR_TYPE_I64;
                                else if (ret_type_node->data.string_value[0] == '*') ret_type = IR_TYPE_PTR;
                            }

                            IRFunction *fn = ir_add_function(b->prog, name, ret_type);
                            fn->is_public = is_pub;

                            // Set up parameters
                            fn->num_params = init_value->data.fn.param_count;
                            fn->params = arena_calloc(&g_arena_compile, fn->num_params, sizeof(char *));
                            fn->param_types = arena_calloc(&g_arena_compile, fn->num_params, sizeof(IRType));
                            for (int j = 0; j < fn->num_params; j++) {
                                ASTNode *param = init_value->data.fn.params[j];
                                fn->params[j] = ir_strdup(&g_arena_compile, param->data.list.children[0]->data.string_value);
                                ASTNode *pt = param->data.list.children[1];
                                if (pt->type == AST_IDENTIFIER) {
                                    if (strcmp(pt->data.string_value, "void") == 0) fn->param_types[j] = IR_TYPE_VOID;
                                    else if (strcmp(pt->data.string_value, "int") == 0) fn->param_types[j] = IR_TYPE_I64;
                                    else if (pt->data.string_value[0] == '*') fn->param_types[j] = IR_TYPE_PTR;
                                    else fn->param_types[j] = IR_TYPE_I64;
                                } else {
                                    fn->param_types[j] = IR_TYPE_I64;
                                }
                            }

                            // Create entry block
                            IRBlock *entry = ir_add_block(fn, "entry");
                            ir_set_entry(fn, entry);

                            // Set up builder for this function
                            IRFunction *prev_fn = b->fn;
                            IRBlock *prev_block = b->block;
                            int prev_vars = b->num_vars;
                            int prev_temp = b->temp_counter;
                            int prev_loop = b->loop_depth;

                            b->fn = fn;
                            b->block = entry;
                            b->temp_counter = 0;
                            b->loop_depth = 0;

                            // Add parameters as variables
                            // QBE requires params named p0, p1, etc.
                            for (int j = 0; j < fn->num_params; j++) {
                                char qbe_name[16];
                                snprintf(qbe_name, sizeof(qbe_name), "p%d", j);
                                fn->params[j] = ir_strdup(&g_arena_compile, qbe_name);

                                char *ptr_reg = ir_gen_temp(b);
                                ir_emit(b, ir_instr(IR_ALLOC, op_reg(ptr_reg), op_imm(16), op_none(), op_none(), IR_TYPE_PTR));
                                ir_emit(b, ir_instr(IR_STORE, op_none(), op_reg(qbe_name), op_reg(ptr_reg), op_none(), IR_TYPE_VOID));
                                // Map original param name to the pointer register
                                ASTNode *param = init_value->data.fn.params[j];
                                ir_add_var(b, param->data.list.children[0]->data.string_value, ptr_reg, false, NULL);
                            }

                            // Build function body
                            ASTNode *body = init_value->data.fn.body;
                            bool body_is_expression = (body->type != AST_LIST ||
                                                       body->data.list.count == 0 ||
                                                       (body->type == AST_LIST && body->data.list.count > 0 &&
                                                        body->data.list.children[0]->type == AST_IDENTIFIER &&
                                                        strcmp(body->data.list.children[0]->data.string_value, "begin") != 0 &&
                                                        strcmp(body->data.list.children[0]->data.string_value, "if") != 0 &&
                                                        strcmp(body->data.list.children[0]->data.string_value, "while") != 0 &&
                                                        strcmp(body->data.list.children[0]->data.string_value, "let") != 0 &&
                                                        strcmp(body->data.list.children[0]->data.string_value, "set") != 0 &&
                                                        strcmp(body->data.list.children[0]->data.string_value, "ret") != 0 &&
                                                        strcmp(body->data.list.children[0]->data.string_value, "break") != 0 &&
                                                        strcmp(body->data.list.children[0]->data.string_value, "continue") != 0));

                            if (body_is_expression && ret_type != IR_TYPE_VOID) {
                                IROperand result = ir_build_expr(b, body, symbols);
                                if (result.type != OP_NONE) {
                                    ir_emit(b, ir_instr(IR_RET, op_none(), result, op_none(), op_none(), IR_TYPE_VOID));
                                } else {
                                    ir_emit(b, ir_instr(IR_RET, op_none(), op_imm(0), op_none(), op_none(), IR_TYPE_VOID));
                                }
                            } else {
                                ir_build_stmt(b, body, symbols);

                                // Add implicit return if needed
                                if ((b->block && b->block->num_instrs == 0) ||
                                    (b->block && b->block->num_instrs > 0 &&
                                     b->block->instrs[b->block->num_instrs - 1].op != IR_JMP &&
                                     b->block->instrs[b->block->num_instrs - 1].op != IR_RET &&
                                     b->block->instrs[b->block->num_instrs - 1].op != IR_JNZ)) {
                                    if (ret_type == IR_TYPE_VOID) {
                                        ir_emit(b, ir_instr(IR_RET, op_none(), op_none(), op_none(), op_none(), IR_TYPE_VOID));
                                    } else {
                                        ir_emit(b, ir_instr(IR_RET, op_none(), op_imm(0), op_none(), op_none(), IR_TYPE_VOID));
                                    }
                                }
                            }

                            // Restore builder state
                            b->fn = prev_fn;
                            b->block = prev_block;
                            // Remove variables added for this function
                            b->num_vars = prev_vars;
                            b->temp_counter = prev_temp;
                            b->loop_depth = prev_loop;

                            // Skip _main if this is main and we're generating it separately
                            continue;
                        }

                        ir_add_global_from_init(b, name, init_value, stmt->data.list.count >= 4 ? stmt->data.list.children[2] : NULL, is_pub);
                    }
                    continue;
                }
            }
        }
    }
}

// --- Check if AST uses exponentiation ---

// --- Generate __clumsy_pow helper function using direct IR emission ---
// Uses stack-based variables for clean loop structure
static void ir_generate_pow_function(IRBuilder *b, IRProgram *prog) {
    IRFunction *prev_fn = b->fn;
    IRBlock *prev_block = b->block;
    int prev_vars = b->num_vars;
    int prev_temp = b->temp_counter;

    IRFunction *fn = ir_add_function(prog, "__clumsy_pow", IR_TYPE_I64);
    fn->is_public = false;
    fn->num_params = 2;
    fn->params = arena_calloc(&g_arena_compile, 2, sizeof(char *));
    fn->param_types = arena_calloc(&g_arena_compile, 2, sizeof(IRType));
    fn->params[0] = ir_strdup(&g_arena_compile, "p0");
    fn->params[1] = ir_strdup(&g_arena_compile, "p1");
    fn->param_types[0] = IR_TYPE_I64;
    fn->param_types[1] = IR_TYPE_I64;

    b->fn = fn;
    b->temp_counter = 0;

    IRBlock *entry = ir_add_block(fn, "entry");
    IRBlock *loop = ir_add_block(fn, "loop");
    IRBlock *check_odd = ir_add_block(fn, "check_odd");
    IRBlock *mul_result = ir_add_block(fn, "mul_result");
    IRBlock *square = ir_add_block(fn, "square");
    IRBlock *done = ir_add_block(fn, "done");
    IRBlock *ret_zero = ir_add_block(fn, "ret_zero");
    ir_set_entry(fn, entry);

    // --- Entry block: store params, init result ---
    b->block = entry;

    char *base_slot = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_ALLOC, op_reg(base_slot), op_imm(8), op_none(), op_none(), IR_TYPE_PTR));
    ir_emit(b, ir_instr(IR_STORE, op_none(), op_reg("p0"), op_reg(base_slot), op_none(), IR_TYPE_VOID));

    char *exp_slot = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_ALLOC, op_reg(exp_slot), op_imm(8), op_none(), op_none(), IR_TYPE_PTR));
    ir_emit(b, ir_instr(IR_STORE, op_none(), op_reg("p1"), op_reg(exp_slot), op_none(), IR_TYPE_VOID));

    char *result_slot = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_ALLOC, op_reg(result_slot), op_imm(8), op_none(), op_none(), IR_TYPE_PTR));
    ir_emit(b, ir_instr(IR_STORE, op_none(), op_imm(1), op_reg(result_slot), op_none(), IR_TYPE_VOID));

    ir_emit(b, ir_instr(IR_JMP, op_none(), op_block(loop->label), op_none(), op_none(), IR_TYPE_VOID));

    // --- Loop: load exp, check if done ---
    b->block = loop;
    char *exp_val = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_LOAD, op_reg(exp_val), op_reg(exp_slot), op_none(), op_none(), IR_TYPE_I64));

    char *zero_check = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_EQ, op_reg(zero_check), op_reg(exp_val), op_imm(0), op_none(), IR_TYPE_I64));
    ir_emit(b, ir_instr(IR_JNZ, op_none(), op_reg(zero_check), op_block(done->label), op_block(check_odd->label), IR_TYPE_VOID));

    // --- Check odd: load exp again from stack ---
    b->block = check_odd;
    char *exp_val2 = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_LOAD, op_reg(exp_val2), op_reg(exp_slot), op_none(), op_none(), IR_TYPE_I64));
    char *and_tmp = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_AND, op_reg(and_tmp), op_reg(exp_val2), op_imm(1), op_none(), IR_TYPE_I64));
    char *odd_check = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_NE, op_reg(odd_check), op_reg(and_tmp), op_imm(0), op_none(), IR_TYPE_I64));
    ir_emit(b, ir_instr(IR_JNZ, op_none(), op_reg(odd_check), op_block(mul_result->label), op_block(square->label), IR_TYPE_VOID));

    // --- mul_result: result *= base; exp -= 1 ---
    b->block = mul_result;
    char *base_val1 = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_LOAD, op_reg(base_val1), op_reg(base_slot), op_none(), op_none(), IR_TYPE_I64));
    char *result_val1 = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_LOAD, op_reg(result_val1), op_reg(result_slot), op_none(), op_none(), IR_TYPE_I64));
    char *new_result1 = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_MUL, op_reg(new_result1), op_reg(result_val1), op_reg(base_val1), op_none(), IR_TYPE_I64));
    ir_emit(b, ir_instr(IR_STORE, op_none(), op_reg(new_result1), op_reg(result_slot), op_none(), IR_TYPE_VOID));

    char *exp_val3 = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_LOAD, op_reg(exp_val3), op_reg(exp_slot), op_none(), op_none(), IR_TYPE_I64));
    char *new_exp1 = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_SUB, op_reg(new_exp1), op_reg(exp_val3), op_imm(1), op_none(), IR_TYPE_I64));
    ir_emit(b, ir_instr(IR_STORE, op_none(), op_reg(new_exp1), op_reg(exp_slot), op_none(), IR_TYPE_VOID));

    ir_emit(b, ir_instr(IR_JMP, op_none(), op_block(loop->label), op_none(), op_none(), IR_TYPE_VOID));

    // --- square: base *= base; exp >>= 1 ---
    b->block = square;
    char *base_val2 = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_LOAD, op_reg(base_val2), op_reg(base_slot), op_none(), op_none(), IR_TYPE_I64));
    char *new_base = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_MUL, op_reg(new_base), op_reg(base_val2), op_reg(base_val2), op_none(), IR_TYPE_I64));
    ir_emit(b, ir_instr(IR_STORE, op_none(), op_reg(new_base), op_reg(base_slot), op_none(), IR_TYPE_VOID));

    char *exp_val4 = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_LOAD, op_reg(exp_val4), op_reg(exp_slot), op_none(), op_none(), IR_TYPE_I64));
    char *new_exp2 = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_SHR, op_reg(new_exp2), op_reg(exp_val4), op_imm(1), op_none(), IR_TYPE_I64));
    ir_emit(b, ir_instr(IR_STORE, op_none(), op_reg(new_exp2), op_reg(exp_slot), op_none(), IR_TYPE_VOID));

    ir_emit(b, ir_instr(IR_JMP, op_none(), op_block(loop->label), op_none(), op_none(), IR_TYPE_VOID));

    // --- done: return result ---
    b->block = done;
    char *final_result = ir_gen_temp(b);
    ir_emit(b, ir_instr(IR_LOAD, op_reg(final_result), op_reg(result_slot), op_none(), op_none(), IR_TYPE_I64));
    ir_emit(b, ir_instr(IR_RET, op_none(), op_reg(final_result), op_none(), op_none(), IR_TYPE_VOID));

    // --- ret_zero: return 0 ---
    b->block = ret_zero;
    ir_emit(b, ir_instr(IR_RET, op_none(), op_imm(0), op_none(), op_none(), IR_TYPE_VOID));

    // Restore builder state
    b->fn = prev_fn;
    b->block = prev_block;
    b->num_vars = prev_vars;
    b->temp_counter = prev_temp;
}

// --- Public API ---

IRProgram *ir_build(ASTNode *ast, SymbolTable *symbols) {
    arena_reset(&g_arena_scratch);
    IRProgram *prog = ir_new_program();
    IRBuilder builder;
    ir_builder_init(&builder, prog);

    ir_build_top_level(&builder, ast, symbols);

    // Generate __clumsy_pow if exponentiation is used anywhere
    if (ast_uses_exponentiation(ast)) {
        ir_generate_pow_function(&builder, prog);
    }

    ir_builder_free(&builder);
    return prog;
}
