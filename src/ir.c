#include "ir.h"

// --- String helpers ---

char *ir_strdup(Arena *a, const char *s) {
    return arena_strdup(a, s);
}

// --- Operand constructors ---

IROperand op_none(void) {
    IROperand o = {0};
    o.type = OP_NONE;
    return o;
}

IROperand op_imm(int64_t value) {
    IROperand o = {0};
    o.type = OP_IMM;
    o.imm = value;
    return o;
}

IROperand op_reg(const char *name) {
    IROperand o = {0};
    o.type = OP_REGISTER;
    o.reg = ir_strdup(&g_arena_compile, name);
    return o;
}

IROperand op_vreg(int id) {
    IROperand o = {0};
    o.type = OP_VREG;
    // Encode vreg id as a string "%v_N"
    char buf[32];
    snprintf(buf, sizeof(buf), "%%v_%d", id);
    o.reg = ir_strdup(&g_arena_compile, buf);
    return o;
}

IROperand op_block(const char *label) {
    IROperand o = {0};
    o.type = OP_BLOCK;
    o.block = ir_strdup(&g_arena_compile, label);
    return o;
}

IROperand op_global(const char *name) {
    IROperand o = {0};
    o.type = OP_GLOBAL;
    o.global = ir_strdup(&g_arena_compile, name);
    return o;
}

// --- Instruction constructors ---

IRInstr ir_instr(IROpcode op, IROperand dest, IROperand a0, IROperand a1, IROperand a2, IRType dest_type) {
    IRInstr i = {0};
    i.op = op;
    i.dest = dest;
    i.args[0] = a0;
    i.args[1] = a1;
    i.args[2] = a2;
    i.dest_type = dest_type;
    return i;
}

IRInstr ir_call(IROperand dest, IROperand *args, int num_args, IRType dest_type) {
    IRInstr i = {0};
    i.op = IR_CALL;
    i.dest = dest;
    i.dest_type = dest_type;
    for (int j = 0; j < num_args && j < 8; j++) {
        i.args[j] = args[j];
    }
    return i;
}

IRInstr ir_phi(IROperand dest, IROperand *pairs, int count, IRType dest_type) {
    IRInstr i = {0};
    i.op = IR_PHI;
    i.dest = dest;
    i.phi_count = count;
    i.dest_type = dest_type;
    for (int j = 0; j < count * 2 && j < 8; j++) {
        i.phi_pairs[j] = pairs[j];
    }
    return i;
}

// --- Basic block ---

IRBlock *ir_new_block(const char *label) {
    IRBlock *b = arena_calloc(&g_arena_compile, 1, sizeof(IRBlock));
    b->label = ir_strdup(&g_arena_compile, label);
    b->capacity = 16;
    b->instrs = arena_calloc(&g_arena_compile, b->capacity, sizeof(IRInstr));
    b->num_instrs = 0;
    b->succ_capacity = 4;
    b->succs = arena_calloc(&g_arena_compile, b->succ_capacity, sizeof(IRBlock *));
    b->num_succs = 0;
    b->pred_capacity = 4;
    b->preds = arena_calloc(&g_arena_compile, b->pred_capacity, sizeof(IRBlock *));
    b->num_preds = 0;
    b->df_capacity = 4;
    b->dom_frontiers = arena_calloc(&g_arena_compile, b->df_capacity, sizeof(IRBlock *));
    b->num_dom_frontiers = 0;
    return b;
}

IROperand ir_new_vreg(IRFunction *fn) {
    return op_vreg(fn->vreg_counter++);
}

void ir_append_instr(IRBlock *block, IRInstr instr) {
    if (block->num_instrs >= block->capacity) {
        int old_capacity = block->capacity;
        block->capacity *= 2;
        block->instrs = arena_grow(&g_arena_compile, block->instrs,
                                   (size_t)old_capacity * sizeof(IRInstr),
                                   (size_t)block->capacity * sizeof(IRInstr));
    }
    block->instrs[block->num_instrs++] = instr;
}

// --- IR Function ---

IRFunction *ir_new_function(const char *name, IRType return_type) {
    IRFunction *fn = arena_calloc(&g_arena_compile, 1, sizeof(IRFunction));
    fn->name = ir_strdup(&g_arena_compile, name);
    fn->return_type = return_type;
    fn->block_capacity = 16;
    fn->blocks = arena_calloc(&g_arena_compile, fn->block_capacity, sizeof(IRBlock *));
    fn->num_blocks = 0;
    fn->string_capacity = 16;
    fn->strings = arena_calloc(&g_arena_compile, fn->string_capacity, sizeof(fn->strings[0]));
    fn->num_strings = 0;
    fn->label_counter = 0;
    return fn;
}

IRBlock *ir_add_block(IRFunction *fn, const char *label) {
    if (fn->num_blocks >= fn->block_capacity) {
        int old_capacity = fn->block_capacity;
        fn->block_capacity *= 2;
        fn->blocks = arena_grow(&g_arena_compile, fn->blocks,
                                (size_t)old_capacity * sizeof(IRBlock *),
                                (size_t)fn->block_capacity * sizeof(IRBlock *));
    }
    IRBlock *b = ir_new_block(label);
    fn->blocks[fn->num_blocks++] = b;
    return b;
}

void ir_set_entry(IRFunction *fn, IRBlock *block) {
    fn->entry = block;
}

char *ir_gen_label(IRFunction *fn, const char *prefix) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s_%d", prefix, fn->label_counter++);
    return ir_strdup(&g_arena_scratch, buf);
}

void ir_add_string(IRFunction *fn, const char *label, const char *value) {
    if (fn->num_strings >= fn->string_capacity) {
        int old_capacity = fn->string_capacity;
        fn->string_capacity *= 2;
        fn->strings = arena_grow(&g_arena_compile, fn->strings,
                                 (size_t)old_capacity * sizeof(fn->strings[0]),
                                 (size_t)fn->string_capacity * sizeof(fn->strings[0]));
    }
    fn->strings[fn->num_strings].label = ir_strdup(&g_arena_compile, label);
    fn->strings[fn->num_strings].value = ir_strdup(&g_arena_compile, value);
    fn->num_strings++;
}

// --- IR Program ---

IRProgram *ir_new_program(void) {
    IRProgram *prog = arena_calloc(&g_arena_compile, 1, sizeof(IRProgram));
    prog->func_capacity = 16;
    prog->functions = arena_calloc(&g_arena_compile, prog->func_capacity, sizeof(IRFunction *));
    prog->num_functions = 0;
    prog->extern_capacity = 16;
    prog->externs = arena_calloc(&g_arena_compile, prog->extern_capacity, sizeof(prog->externs[0]));
    prog->num_externs = 0;
    prog->global_capacity = 16;
    prog->globals = arena_calloc(&g_arena_compile, prog->global_capacity, sizeof(prog->globals[0]));
    prog->num_globals = 0;
    return prog;
}

IRFunction *ir_add_function(IRProgram *prog, const char *name, IRType ret) {
    if (prog->num_functions >= prog->func_capacity) {
        int old_capacity = prog->func_capacity;
        prog->func_capacity *= 2;
        prog->functions = arena_grow(&g_arena_compile, prog->functions,
                                     (size_t)old_capacity * sizeof(IRFunction *),
                                     (size_t)prog->func_capacity * sizeof(IRFunction *));
    }
    IRFunction *fn = ir_new_function(name, ret);
    prog->functions[prog->num_functions++] = fn;
    return fn;
}

void ir_add_extern(IRProgram *prog, const char *name, IRType ret, IRType *params, int num_params, bool variadic) {
    if (prog->num_externs >= prog->extern_capacity) {
        int old_capacity = prog->extern_capacity;
        prog->extern_capacity *= 2;
        prog->externs = arena_grow(&g_arena_compile, prog->externs,
                                   (size_t)old_capacity * sizeof(prog->externs[0]),
                                   (size_t)prog->extern_capacity * sizeof(prog->externs[0]));
    }
    int i = prog->num_externs++;
    prog->externs[i].name = ir_strdup(&g_arena_compile, name);
    prog->externs[i].return_type = ret;
    prog->externs[i].num_params = num_params;
    prog->externs[i].is_variadic = variadic;
    if (num_params > 0) {
        prog->externs[i].param_types = arena_calloc(&g_arena_compile, num_params, sizeof(IRType));
        memcpy(prog->externs[i].param_types, params, num_params * sizeof(IRType));
    } else {
        prog->externs[i].param_types = NULL;
    }
}

void ir_add_global(IRProgram *prog, const char *name, int64_t value, bool is_string, const char *str_val, int array_size, bool is_public) {
    if (prog->num_globals >= prog->global_capacity) {
        int old_capacity = prog->global_capacity;
        prog->global_capacity *= 2;
        prog->globals = arena_grow(&g_arena_compile, prog->globals,
                                   (size_t)old_capacity * sizeof(prog->globals[0]),
                                   (size_t)prog->global_capacity * sizeof(prog->globals[0]));
    }
    int i = prog->num_globals++;
    prog->globals[i].name = ir_strdup(&g_arena_compile, name);
    prog->globals[i].value = value;
    prog->globals[i].is_string = is_string;
    prog->globals[i].str_val = str_val ? ir_strdup(&g_arena_compile, str_val) : NULL;
    prog->globals[i].array_size = array_size;
    prog->globals[i].is_public = is_public;
    prog->globals[i].struct_fields = NULL;
    prog->globals[i].num_struct_fields = 0;
    prog->globals[i].is_struct = false;
    prog->globals[i].array_values = NULL;
    prog->globals[i].is_extern = false;
}

void ir_add_global_array(IRProgram *prog, const char *name, int64_t *values, int array_size, bool is_public) {
    if (prog->num_globals >= prog->global_capacity) {
        int old_capacity = prog->global_capacity;
        prog->global_capacity *= 2;
        prog->globals = arena_grow(&g_arena_compile, prog->globals,
                                   (size_t)old_capacity * sizeof(prog->globals[0]),
                                   (size_t)prog->global_capacity * sizeof(prog->globals[0]));
    }
    int i = prog->num_globals++;
    prog->globals[i].name = ir_strdup(&g_arena_compile, name);
    prog->globals[i].value = (values && array_size > 0) ? values[0] : 0;
    prog->globals[i].is_string = false;
    prog->globals[i].str_val = NULL;
    prog->globals[i].array_size = array_size;
    prog->globals[i].is_public = is_public;
    prog->globals[i].struct_fields = NULL;
    prog->globals[i].num_struct_fields = 0;
    prog->globals[i].is_struct = false;
    prog->globals[i].is_extern = false;
    if (values && array_size > 0) {
        prog->globals[i].array_values = arena_calloc(&g_arena_compile, array_size, sizeof(int64_t));
        memcpy(prog->globals[i].array_values, values, array_size * sizeof(int64_t));
    } else {
        prog->globals[i].array_values = NULL;
    }
}

void ir_add_struct_global(IRProgram *prog, const char *name, char **fields, int num_fields, bool is_public) {
    if (prog->num_globals >= prog->global_capacity) {
        int old_capacity = prog->global_capacity;
        prog->global_capacity *= 2;
        prog->globals = arena_grow(&g_arena_compile, prog->globals,
                                   (size_t)old_capacity * sizeof(prog->globals[0]),
                                   (size_t)prog->global_capacity * sizeof(prog->globals[0]));
    }
    int i = prog->num_globals++;
    prog->globals[i].name = ir_strdup(&g_arena_compile, name);
    prog->globals[i].value = 0;
    prog->globals[i].is_string = false;
    prog->globals[i].str_val = NULL;
    prog->globals[i].array_size = 0;
    prog->globals[i].is_public = is_public;
    prog->globals[i].is_struct = true;
    prog->globals[i].is_extern = false;
    prog->globals[i].array_values = NULL;
    prog->globals[i].num_struct_fields = num_fields;
    prog->globals[i].struct_fields = arena_calloc(&g_arena_compile, num_fields, sizeof(char *));
    for (int j = 0; j < num_fields; j++) {
        prog->globals[i].struct_fields[j] = ir_strdup(&g_arena_compile, fields[j]);
    }
}

// --- Cleanup ---

void free_ir_program(IRProgram *prog) {
    (void)prog;
}

// --- Printing ---

const char *ir_opcode_name(IROpcode op) {
    switch (op) {
        case IR_ADD: return "add";
        case IR_SUB: return "sub";
        case IR_MUL: return "mul";
        case IR_DIV: return "div";
        case IR_MOD: return "mod";
        case IR_AND: return "and";
        case IR_OR:  return "or";
        case IR_XOR: return "xor";
        case IR_SHL: return "shl";
        case IR_SHR: return "shr";
        case IR_NOT: return "not";
        case IR_NEG: return "neg";
        case IR_EQ:  return "ceq";
        case IR_NE:  return "cne";
        case IR_LT:  return "clt";
        case IR_GT:  return "cgt";
        case IR_LE:  return "cle";
        case IR_GE:  return "cge";
        case IR_LOAD:  return "load";
        case IR_STORE: return "store";
        case IR_ALLOC: return "alloc";
        case IR_JMP:   return "jmp";
        case IR_JNZ:   return "jnz";
        case IR_RET:   return "ret";
        case IR_CALL:  return "call";
        case IR_PHI:   return "phi";
        case IR_MOV:   return "mov";
        case IR_ADDR:  return "addr";
    }
    return "???";
}

const char *ir_type_name(IRType type) {
    switch (type) {
        case IR_TYPE_VOID: return "void";
        case IR_TYPE_I64:  return "l";  // QBE uses 'l' for 64-bit
        case IR_TYPE_PTR:  return "l";  // pointers are also 64-bit
    }
    return "?";
}

static void ir_print_operand(IROperand *op, FILE *out) {
    switch (op->type) {
        case OP_NONE: break;
        case OP_IMM: fprintf(out, "%ld", (long)op->imm); break;
        case OP_REGISTER: fprintf(out, "%%%s", op->reg); break;
        case OP_VREG: fprintf(out, "%s", op->reg); break;
        case OP_BLOCK: fprintf(out, "@%s", op->block); break;
        case OP_GLOBAL: fprintf(out, "$%s", op->global); break;
    }
}

void ir_print_instr(IRInstr *instr, FILE *out) {
    // Print dest if present
    if (instr->dest.type != OP_NONE) {
        ir_print_operand(&instr->dest, out);
        fprintf(out, "=%s ", ir_type_name(instr->dest_type));
    }

    fprintf(out, "%s", ir_opcode_name(instr->op));

    switch (instr->op) {
        case IR_PHI:
            fprintf(out, " ");
            for (int i = 0; i < instr->phi_count; i++) {
                if (i > 0) fprintf(out, ", ");
                fprintf(out, "[");
                ir_print_operand(&instr->phi_pairs[i * 2], out);
                fprintf(out, ", @%s", instr->phi_pairs[i * 2 + 1].block);
                fprintf(out, "]");
            }
            break;

        case IR_JMP:
            fprintf(out, " ");
            ir_print_operand(&instr->args[0], out);
            break;

        case IR_JNZ:
            fprintf(out, " ");
            ir_print_operand(&instr->args[0], out);
            fprintf(out, ", ");
            ir_print_operand(&instr->args[1], out);
            fprintf(out, ", ");
            ir_print_operand(&instr->args[2], out);
            break;

        case IR_RET:
            if (instr->args[0].type != OP_NONE) {
                fprintf(out, " ");
                ir_print_operand(&instr->args[0], out);
            }
            break;

        case IR_STORE:
            fprintf(out, " ");
            ir_print_operand(&instr->args[0], out);
            fprintf(out, ", ");
            ir_print_operand(&instr->args[1], out);
            if (instr->args[2].type != OP_NONE) {
                fprintf(out, ", ");
                ir_print_operand(&instr->args[2], out);
            }
            break;

        case IR_CALL:
            fprintf(out, " ");
            ir_print_operand(&instr->args[0], out);
            fprintf(out, "(");
            for (int i = 1; i < 5 && instr->args[i].type != OP_NONE; i++) {
                if (i > 1) fprintf(out, ", ");
                ir_print_operand(&instr->args[i], out);
            }
            fprintf(out, ")");
            break;

        default:
            // Binary/unary ops: op arg0, arg1 [, arg2]
            for (int i = 0; i < 3 && instr->args[i].type != OP_NONE; i++) {
                fprintf(out, " ");
                ir_print_operand(&instr->args[i], out);
            }
            break;
    }
}

void ir_print_block(IRBlock *block, FILE *out) {
    fprintf(out, "@%s:\n", block->label);
    for (int i = 0; i < block->num_instrs; i++) {
        fprintf(out, "    ");
        ir_print_instr(&block->instrs[i], out);
        fprintf(out, "\n");
    }
}

void ir_print_function(IRFunction *fn, FILE *out) {
    fprintf(out, "function %s %s(", ir_type_name(fn->return_type), fn->name);
    for (int i = 0; i < fn->num_params; i++) {
        if (i > 0) fprintf(out, ", ");
        fprintf(out, "%s %%p%d", ir_type_name(fn->param_types[i]), i);
    }
    fprintf(out, ") {\n");

    for (int i = 0; i < fn->num_blocks; i++) {
        ir_print_block(fn->blocks[i], out);
    }

    fprintf(out, "}\n\n");
}

// Whether a call to a global target needs the variadic ABI. True if the call
// is flagged variadic (print/println builtins), the extern was declared
// variadic, or more args are passed than the extern declares fixed params
// (printf called through a non-variadic decl). Own functions are never
// variadic. This rule mirrors the cranelift backend's mark_call_variadic.
bool ir_call_is_variadic(IRProgram *prog, IRInstr *in) {
    if (in->op != IR_CALL || in->args[0].type != OP_GLOBAL) return false;
    const char *name = in->args[0].global;
    if (in->is_variadic) return true;

    int nargs = 0;
    for (int a = 1; a < 8 && in->args[a].type != OP_NONE; a++) nargs++;

    // printf is a libc variadic function even when the source declared it with
    // fixed params (e.g. [(* char) int]): any call that passes something after
    // the format string must use the stack-varargs stub.
    if (strcmp(name, "printf") == 0 && nargs >= 2) return true;

    int ei = -1;
    for (int i = 0; i < prog->num_externs; i++) {
        if (strcmp(prog->externs[i].name, name) == 0) { ei = i; break; }
    }
    if (ei >= 0 && prog->externs[ei].is_variadic) return true;
    if (ei >= 0 && nargs > prog->externs[ei].num_params) return true;
    return false;
}

// Whether any instruction in the program calls the named global target.
bool ir_program_calls(IRProgram *prog, const char *name) {
    for (int f = 0; f < prog->num_functions; f++) {
        IRFunction *fn = prog->functions[f];
        for (int b = 0; b < fn->num_blocks; b++) {
            IRBlock *blk = fn->blocks[b];
            for (int k = 0; k < blk->num_instrs; k++) {
                IRInstr *in = &blk->instrs[k];
                if (in->op == IR_CALL && in->args[0].type == OP_GLOBAL &&
                    strcmp(in->args[0].global, name) == 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

// Whether an extern is treated as variadic: printf is always a libc variadic
// function, and any extern with at least one variadic call site (see
// ir_call_is_variadic) is routed entirely through its fixed-arity stub so the
// emitters never need a mixed direct/stub declaration.
bool ir_extern_is_variadic(IRProgram *prog, const char *name) {
    if (strcmp(name, "printf") == 0) return true;
    for (int f = 0; f < prog->num_functions; f++) {
        IRFunction *fn = prog->functions[f];
        for (int b = 0; b < fn->num_blocks; b++) {
            IRBlock *blk = fn->blocks[b];
            for (int k = 0; k < blk->num_instrs; k++) {
                IRInstr *in = &blk->instrs[k];
                if (in->op == IR_CALL && in->args[0].type == OP_GLOBAL &&
                    strcmp(in->args[0].global, name) == 0 &&
                    ir_call_is_variadic(prog, in)) {
                    return true;
                }
            }
        }
    }
    return false;
}

void ir_print_program(IRProgram *prog, FILE *out) {
    // Externs
    for (int i = 0; i < prog->num_externs; i++) {
        fprintf(out, "extern %s %s(", ir_type_name(prog->externs[i].return_type), prog->externs[i].name);
        for (int j = 0; j < prog->externs[i].num_params; j++) {
            if (j > 0) fprintf(out, ", ");
            fprintf(out, "%s", ir_type_name(prog->externs[i].param_types[j]));
        }
        if (prog->externs[i].is_variadic) fprintf(out, ", ...");
        fprintf(out, ")\n");
    }
    if (prog->num_externs > 0) fprintf(out, "\n");

    // Globals
    for (int i = 0; i < prog->num_globals; i++) {
        fprintf(out, "data $%s = { ", prog->globals[i].name);
        if (prog->globals[i].is_string) {
            fprintf(out, "b \"%s\", b 0", prog->globals[i].str_val);
        } else if (prog->globals[i].array_size > 0) {
            for (int j = 0; j < prog->globals[i].array_size; j++) {
                if (j > 0) fprintf(out, ", ");
                fprintf(out, "l %ld", (long)prog->globals[i].value);
            }
        } else {
            fprintf(out, "l %ld", (long)prog->globals[i].value);
        }
        fprintf(out, " }\n");
    }
    if (prog->num_globals > 0) fprintf(out, "\n");

    // Functions
    for (int i = 0; i < prog->num_functions; i++) {
        ir_print_function(prog->functions[i], out);
    }
}
