#include "ir.h"
#include <ctype.h>

// QBE emitter: IR → QBE IL text
//
// QBE conventions:
//   Types: w (32-bit), l (64-bit), s (float), d (double), b (byte)
//   %name = local, $name = global/extern, @label = block (no colon)
//   Instruction format: %dest =TYPE opcode args
//   Stack alloc: alloc4/alloc8/alloc16 (alignment-based)
//   Functions: export function l $name(l %p0, l %p1) { @entry ... }
//
// QBE integer comparisons:
//   ceql, cnel (equality)
//   csltl, csgtl, cslel, csgel (signed comparisons)
//   cultl, cugtl, culel, cugel (unsigned comparisons)
//   Result type is w (32-bit), operands can be w or l

// QBE type for the =TYPE annotation on instructions
static const char *qbe_itype(IRType type) {
    switch (type) {
        case IR_TYPE_I64: return "l";
        case IR_TYPE_PTR: return "l";
        default: return "w";
    }
}

// Sanitize a name for QBE: replace non-alphanumeric/underscore chars
static char *qbe_sanitize(const char *name) {
    int len = strlen(name);
    char *result = malloc(len + 1);
    for (int i = 0; i < len; i++) {
        char c = name[i];
        if (isalnum(c) || c == '_') {
            result[i] = c;
        } else {
            result[i] = '_';
        }
    }
    result[len] = '\0';
    return result;
}

static const char *qbe_type_name(IRType type) {
    switch (type) {
        case IR_TYPE_I64: return "l";
        case IR_TYPE_PTR: return "l";
        case IR_TYPE_VOID: return "w";  // QBE has no void; use w as placeholder
        default: return "l";
    }
}

static void emit_qbe_operand(IROperand *op, FILE *out) {
    switch (op->type) {
        case OP_NONE: break;
        case OP_IMM: fprintf(out, "%ld", (long)op->imm); break;
        case OP_REGISTER: fprintf(out, "%%%s", op->reg); break;
        case OP_BLOCK: fprintf(out, "@%s", op->block); break;
        case OP_GLOBAL: {
            char *s = qbe_sanitize(op->global);
            fprintf(out, "$%s", s);
            free(s);
            break;
        }
    }
}

// QBE comparison opcode for IR comparison ops (signed long)
static const char *qbe_cmp_opcode(IROpcode op) {
    switch (op) {
        case IR_EQ: return "ceql";
        case IR_NE: return "cnel";
        case IR_LT: return "csltl";   // signed less than, long
        case IR_GT: return "csgtl";   // signed greater than, long
        case IR_LE: return "cslel";   // signed less or equal, long
        case IR_GE: return "csgel";   // signed greater or equal, long
        default: return "ceql";
    }
}

static void emit_qbe_instr(IRInstr *instr, FILE *out) {
    switch (instr->op) {
        case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV:
        case IR_AND: case IR_OR: case IR_XOR: case IR_SHL: case IR_SHR:
            emit_qbe_operand(&instr->dest, out);
            fprintf(out, " =%s %s ", qbe_itype(instr->dest_type), ir_opcode_name(instr->op));
            emit_qbe_operand(&instr->args[0], out);
            fprintf(out, ", ");
            emit_qbe_operand(&instr->args[1], out);
            break;

        case IR_MOD:
            // QBE uses "rem" not "mod"
            emit_qbe_operand(&instr->dest, out);
            fprintf(out, " =%s rem ", qbe_itype(instr->dest_type));
            emit_qbe_operand(&instr->args[0], out);
            fprintf(out, ", ");
            emit_qbe_operand(&instr->args[1], out);
            break;

        case IR_NOT:
            emit_qbe_operand(&instr->dest, out);
            fprintf(out, " =%s xor ", qbe_itype(instr->dest_type));
            emit_qbe_operand(&instr->args[0], out);
            fprintf(out, ", -1");
            break;

        case IR_NEG:
            emit_qbe_operand(&instr->dest, out);
            fprintf(out, " =%s sub 0, ", qbe_itype(instr->dest_type));
            emit_qbe_operand(&instr->args[0], out);
            break;

        case IR_EQ: case IR_NE: case IR_LT: case IR_GT: case IR_LE: case IR_GE: {
            // QBE comparisons produce w (32-bit) results.
            // We use a temp w register, then extend to l.
            const char *cmp_op = qbe_cmp_opcode(instr->op);
            fprintf(out, "%%_cmp_%s =w %s ", instr->dest.reg, cmp_op);
            emit_qbe_operand(&instr->args[0], out);
            fprintf(out, ", ");
            emit_qbe_operand(&instr->args[1], out);
            fprintf(out, "\n\t");
            emit_qbe_operand(&instr->dest, out);
            fprintf(out, " =l extsw %%_cmp_%s", instr->dest.reg);
            break;
        }

        case IR_LOAD:
            emit_qbe_operand(&instr->dest, out);
            fprintf(out, " =%s loadl ", qbe_itype(instr->dest_type));
            emit_qbe_operand(&instr->args[0], out);
            break;

        case IR_STORE:
            fprintf(out, "storel ");
            emit_qbe_operand(&instr->args[0], out);
            fprintf(out, ", ");
            emit_qbe_operand(&instr->args[1], out);
            break;

        case IR_ALLOC: {
            // args[0] holds the allocation size in bytes
            int alloc_size = 16; // default
            if (instr->args[0].type == OP_IMM) {
                alloc_size = (int)instr->args[0].imm;
                // Round up to 16-byte alignment
                if (alloc_size <= 0) alloc_size = 16;
                else alloc_size = ((alloc_size + 15) / 16) * 16;
            }
            emit_qbe_operand(&instr->dest, out);
            fprintf(out, " =l alloc16 %d", alloc_size);
            break;
        }

        case IR_JMP:
            fprintf(out, "jmp ");
            emit_qbe_operand(&instr->args[0], out);
            break;

        case IR_JNZ:
            fprintf(out, "jnz ");
            emit_qbe_operand(&instr->args[0], out);
            fprintf(out, ", ");
            emit_qbe_operand(&instr->args[1], out);
            fprintf(out, ", ");
            emit_qbe_operand(&instr->args[2], out);
            break;

        case IR_RET:
            fprintf(out, "ret");
            if (instr->args[0].type != OP_NONE) {
                fprintf(out, " ");
                emit_qbe_operand(&instr->args[0], out);
            }
            break;

        case IR_CALL: {
            if (instr->dest.type != OP_NONE) {
                emit_qbe_operand(&instr->dest, out);
                fprintf(out, " =%s ", qbe_itype(instr->dest_type));
            }
            fprintf(out, "call ");
            emit_qbe_operand(&instr->args[0], out);
            fprintf(out, "(");

            // Emit first arg (named param)
            if (instr->args[1].type != OP_NONE) {
                fprintf(out, "l ");
                emit_qbe_operand(&instr->args[1], out);
            }

            if (instr->is_variadic) {
                // macOS ARM64 ABI: variadic args go on stack after ...
                fprintf(out, ", ...");
                for (int i = 2; i < 8 && instr->args[i].type != OP_NONE; i++) {
                    fprintf(out, ", l ");
                    emit_qbe_operand(&instr->args[i], out);
                }
            } else {
                // Non-variadic: all args in registers
                for (int i = 2; i < 8 && instr->args[i].type != OP_NONE; i++) {
                    fprintf(out, ", l ");
                    emit_qbe_operand(&instr->args[i], out);
                }
            }

            fprintf(out, ")");
            break;
        }

        case IR_PHI:
            emit_qbe_operand(&instr->dest, out);
            fprintf(out, " =%s phi ", qbe_itype(instr->dest_type));
            for (int i = 0; i < instr->phi_count; i++) {
                if (i > 0) fprintf(out, ", ");
                emit_qbe_operand(&instr->phi_pairs[i * 2 + 1], out);
                fprintf(out, " ");
                emit_qbe_operand(&instr->phi_pairs[i * 2], out);
            }
            break;

        case IR_MOV:
            emit_qbe_operand(&instr->dest, out);
            fprintf(out, " =%s copy ", qbe_itype(instr->dest_type));
            emit_qbe_operand(&instr->args[0], out);
            break;

        case IR_ADDR:
            emit_qbe_operand(&instr->dest, out);
            fprintf(out, " =%s copy ", qbe_itype(instr->dest_type));
            emit_qbe_operand(&instr->args[0], out);
            break;
    }
}

// Emit a global string literal (top-level)
static void emit_qbe_string_global(const char *name, const char *value, FILE *out) {
    // Strip surrounding quotes if present
    const char *v = value;
    if (v[0] == '"' && strlen(v) > 1 && v[strlen(v)-1] == '"') {
        v++;
        // Use strndup-like approach
        char *stripped = strdup(v);
        stripped[strlen(stripped)-1] = '\0';
        fprintf(out, "data $%s = { b \"%s\", b 0 }\n", name, stripped);
        free(stripped);
    } else {
        fprintf(out, "data $%s = { b \"%s\", b 0 }\n", name, v);
    }
}

static void emit_qbe_function(IRFunction *fn, FILE *out) {
    if (fn->is_public) fprintf(out, "export ");
    char *fname = qbe_sanitize(fn->name);
    fprintf(out, "function %s $%s(", qbe_type_name(fn->return_type), fname);
    for (int i = 0; i < fn->num_params; i++) {
        if (i > 0) fprintf(out, ", ");
        fprintf(out, "%s %%%s", qbe_type_name(fn->param_types[i]), fn->params[i]);
    }
    fprintf(out, ") {\n");

    for (int i = 0; i < fn->num_blocks; i++) {
        IRBlock *b = fn->blocks[i];
        fprintf(out, "@%s\n", b->label);
        for (int j = 0; j < b->num_instrs; j++) {
            fprintf(out, "\t");
            emit_qbe_instr(&b->instrs[j], out);
            fprintf(out, "\n");
        }
    }

    fprintf(out, "}\n\n");
    free(fname);
}

void emit_qbe(IRProgram *prog, FILE *out) {
    // Emit global data (skip struct type definitions)
    for (int i = 0; i < prog->num_globals; i++) {
        if (prog->globals[i].is_struct) continue; // skip struct type metadata
        if (prog->globals[i].is_extern) continue; // skip extern declarations (no data)
        char *gname = qbe_sanitize(prog->globals[i].name);
        const char *exp = prog->globals[i].is_public ? "export " : "";
        if (prog->globals[i].is_string) {
            fprintf(out, "%s", exp);
            emit_qbe_string_global(gname,
                                   prog->globals[i].str_val, out);
        } else if (prog->globals[i].array_size > 0) {
            if (prog->globals[i].array_values) {
                fprintf(out, "%sdata $%s = { l %ld", exp, gname, (long)prog->globals[i].array_values[0]);
                for (int j = 1; j < prog->globals[i].array_size; j++) {
                    fprintf(out, ", l %ld", (long)prog->globals[i].array_values[j]);
                }
                fprintf(out, " }\n");
            } else {
                fprintf(out, "%sdata $%s = { l %ld", exp, gname, (long)prog->globals[i].value);
                for (int j = 1; j < prog->globals[i].array_size; j++) {
                    fprintf(out, ", l %ld", (long)prog->globals[i].value);
                }
                fprintf(out, " }\n");
            }
        } else {
            fprintf(out, "%sdata $%s = { l %ld }\n",
                    exp, gname, (long)prog->globals[i].value);
        }
        free(gname);
    }

    // Emit function-local string literals as global data (before functions)
    for (int i = 0; i < prog->num_functions; i++) {
        IRFunction *fn = prog->functions[i];
        for (int j = 0; j < fn->num_strings; j++) {
            emit_qbe_string_global(fn->strings[j].label,
                                   fn->strings[j].value, out);
        }
    }

    if (prog->num_globals > 0 || prog->num_functions > 0) fprintf(out, "\n");

    for (int i = 0; i < prog->num_functions; i++) {
        emit_qbe_function(prog->functions[i], out);
    }
}
