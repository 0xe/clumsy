#include "ir.h"

// --- String helpers ---

char *ir_strdup(const char *s) {
    if (!s) return NULL;
    char *dup = malloc(strlen(s) + 1);
    strcpy(dup, s);
    return dup;
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
    o.reg = ir_strdup(name);
    return o;
}

IROperand op_block(const char *label) {
    IROperand o = {0};
    o.type = OP_BLOCK;
    o.block = ir_strdup(label);
    return o;
}

IROperand op_global(const char *name) {
    IROperand o = {0};
    o.type = OP_GLOBAL;
    o.global = ir_strdup(name);
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
    for (int j = 0; j < num_args && j < 6; j++) {
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
    IRBlock *b = calloc(1, sizeof(IRBlock));
    b->label = ir_strdup(label);
    b->capacity = 16;
    b->instrs = calloc(b->capacity, sizeof(IRInstr));
    b->num_instrs = 0;
    b->succ_capacity = 4;
    b->succs = calloc(b->succ_capacity, sizeof(IRBlock *));
    b->num_succs = 0;
    return b;
}

void ir_append_instr(IRBlock *block, IRInstr instr) {
    if (block->num_instrs >= block->capacity) {
        block->capacity *= 2;
        block->instrs = realloc(block->instrs, block->capacity * sizeof(IRInstr));
    }
    block->instrs[block->num_instrs++] = instr;
}

// --- IR Function ---

IRFunction *ir_new_function(const char *name, IRType return_type) {
    IRFunction *fn = calloc(1, sizeof(IRFunction));
    fn->name = ir_strdup(name);
    fn->return_type = return_type;
    fn->block_capacity = 16;
    fn->blocks = calloc(fn->block_capacity, sizeof(IRBlock *));
    fn->num_blocks = 0;
    fn->string_capacity = 16;
    fn->strings = calloc(fn->string_capacity, sizeof(fn->strings[0]));
    fn->num_strings = 0;
    fn->label_counter = 0;
    return fn;
}

IRBlock *ir_add_block(IRFunction *fn, const char *label) {
    if (fn->num_blocks >= fn->block_capacity) {
        fn->block_capacity *= 2;
        fn->blocks = realloc(fn->blocks, fn->block_capacity * sizeof(IRBlock *));
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
    return ir_strdup(buf);
}

void ir_add_string(IRFunction *fn, const char *label, const char *value) {
    if (fn->num_strings >= fn->string_capacity) {
        fn->string_capacity *= 2;
        fn->strings = realloc(fn->strings, fn->string_capacity * sizeof(fn->strings[0]));
    }
    fn->strings[fn->num_strings].label = ir_strdup(label);
    fn->strings[fn->num_strings].value = ir_strdup(value);
    fn->num_strings++;
}

// --- IR Program ---

IRProgram *ir_new_program(void) {
    IRProgram *prog = calloc(1, sizeof(IRProgram));
    prog->func_capacity = 16;
    prog->functions = calloc(prog->func_capacity, sizeof(IRFunction *));
    prog->num_functions = 0;
    prog->extern_capacity = 16;
    prog->externs = calloc(prog->extern_capacity, sizeof(prog->externs[0]));
    prog->num_externs = 0;
    prog->global_capacity = 16;
    prog->globals = calloc(prog->global_capacity, sizeof(prog->globals[0]));
    prog->num_globals = 0;
    return prog;
}

IRFunction *ir_add_function(IRProgram *prog, const char *name, IRType ret) {
    if (prog->num_functions >= prog->func_capacity) {
        prog->func_capacity *= 2;
        prog->functions = realloc(prog->functions, prog->func_capacity * sizeof(IRFunction *));
    }
    IRFunction *fn = ir_new_function(name, ret);
    prog->functions[prog->num_functions++] = fn;
    return fn;
}

void ir_add_extern(IRProgram *prog, const char *name, IRType ret, IRType *params, int num_params, bool variadic) {
    if (prog->num_externs >= prog->extern_capacity) {
        prog->extern_capacity *= 2;
        prog->externs = realloc(prog->externs, prog->extern_capacity * sizeof(prog->externs[0]));
    }
    int i = prog->num_externs++;
    prog->externs[i].name = ir_strdup(name);
    prog->externs[i].return_type = ret;
    prog->externs[i].num_params = num_params;
    prog->externs[i].is_variadic = variadic;
    if (num_params > 0) {
        prog->externs[i].param_types = calloc(num_params, sizeof(IRType));
        memcpy(prog->externs[i].param_types, params, num_params * sizeof(IRType));
    } else {
        prog->externs[i].param_types = NULL;
    }
}

void ir_add_global(IRProgram *prog, const char *name, int64_t value, bool is_string, const char *str_val, int array_size, bool is_public) {
    if (prog->num_globals >= prog->global_capacity) {
        prog->global_capacity *= 2;
        prog->globals = realloc(prog->globals, prog->global_capacity * sizeof(prog->globals[0]));
    }
    int i = prog->num_globals++;
    prog->globals[i].name = ir_strdup(name);
    prog->globals[i].value = value;
    prog->globals[i].is_string = is_string;
    prog->globals[i].str_val = str_val ? ir_strdup(str_val) : NULL;
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
        prog->global_capacity *= 2;
        prog->globals = realloc(prog->globals, prog->global_capacity * sizeof(prog->globals[0]));
    }
    int i = prog->num_globals++;
    prog->globals[i].name = ir_strdup(name);
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
        prog->globals[i].array_values = calloc(array_size, sizeof(int64_t));
        memcpy(prog->globals[i].array_values, values, array_size * sizeof(int64_t));
    } else {
        prog->globals[i].array_values = NULL;
    }
}

void ir_add_struct_global(IRProgram *prog, const char *name, char **fields, int num_fields, bool is_public) {
    if (prog->num_globals >= prog->global_capacity) {
        prog->global_capacity *= 2;
        prog->globals = realloc(prog->globals, prog->global_capacity * sizeof(prog->globals[0]));
    }
    int i = prog->num_globals++;
    prog->globals[i].name = ir_strdup(name);
    prog->globals[i].value = 0;
    prog->globals[i].is_string = false;
    prog->globals[i].str_val = NULL;
    prog->globals[i].array_size = 0;
    prog->globals[i].is_public = is_public;
    prog->globals[i].is_struct = true;
    prog->globals[i].is_extern = false;
    prog->globals[i].array_values = NULL;
    prog->globals[i].num_struct_fields = num_fields;
    prog->globals[i].struct_fields = calloc(num_fields, sizeof(char *));
    for (int j = 0; j < num_fields; j++) {
        prog->globals[i].struct_fields[j] = ir_strdup(fields[j]);
    }
}

// --- Cleanup ---

static void free_ir_block(IRBlock *b) {
    if (!b) return;
    free(b->label);
    for (int i = 0; i < b->num_instrs; i++) {
        IRInstr *ins = &b->instrs[i];
        if (ins->dest.type == OP_REGISTER) free(ins->dest.reg);
        else if (ins->dest.type == OP_GLOBAL) free(ins->dest.global);
        for (int j = 0; j < 5; j++) {
            if (ins->args[j].type == OP_REGISTER) free(ins->args[j].reg);
            else if (ins->args[j].type == OP_GLOBAL) free(ins->args[j].global);
            else if (ins->args[j].type == OP_BLOCK) free(ins->args[j].block);
        }
        for (int j = 0; j < ins->phi_count * 2; j++) {
            if (ins->phi_pairs[j].type == OP_REGISTER) free(ins->phi_pairs[j].reg);
            else if (ins->phi_pairs[j].type == OP_BLOCK) free(ins->phi_pairs[j].block);
        }
    }
    free(b->instrs);
    free(b->succs);
    free(b);
}

static void free_ir_function(IRFunction *fn) {
    if (!fn) return;
    free(fn->name);
    for (int i = 0; i < fn->num_blocks; i++) {
        free_ir_block(fn->blocks[i]);
    }
    free(fn->blocks);
    for (int i = 0; i < fn->num_params; i++) {
        free(fn->params[i]);
    }
    free(fn->params);
    free(fn->param_types);
    for (int i = 0; i < fn->num_strings; i++) {
        free(fn->strings[i].label);
        free(fn->strings[i].value);
    }
    free(fn->strings);
    free(fn);
}

void free_ir_program(IRProgram *prog) {
    if (!prog) return;
    for (int i = 0; i < prog->num_functions; i++) {
        free_ir_function(prog->functions[i]);
    }
    free(prog->functions);
    for (int i = 0; i < prog->num_externs; i++) {
        free(prog->externs[i].name);
        free(prog->externs[i].param_types);
    }
    free(prog->externs);
    for (int i = 0; i < prog->num_globals; i++) {
        free(prog->globals[i].name);
        free(prog->globals[i].str_val);
        free(prog->globals[i].array_values);
        if (prog->globals[i].struct_fields) {
            for (int j = 0; j < prog->globals[i].num_struct_fields; j++) {
                free(prog->globals[i].struct_fields[j]);
            }
            free(prog->globals[i].struct_fields);
        }
    }
    free(prog->globals);
    free(prog);
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
