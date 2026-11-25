#include "ir.h"
#include "arena.h"
#include "emit_common.h"

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

typedef struct {
    IRIndex index;
    int cmp_counter;
} QBEEmitter;

// QBE type for the =TYPE annotation on instructions
static const char *qbe_itype(IRType type) {
    switch (type) {
        case IR_TYPE_I64: return "l";
        case IR_TYPE_PTR: return "l";
        default: return "w";
    }
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
        case OP_VREG: fprintf(out, "%s", op->reg); break;
        case OP_BLOCK: fprintf(out, "@%s", op->block); break;
        case OP_GLOBAL: {
            char *s = emit_sanitize(op->global);
            fprintf(out, "$%s", s);
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

static void emit_qbe_instr(QBEEmitter *e, IRInstr *instr, FILE *out, IRProgram *prog) {
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
            int cmp_id = e->cmp_counter++;
            const char *cmp_op = qbe_cmp_opcode(instr->op);
            fprintf(out, "%%_cmp_%d =w %s ", cmp_id, cmp_op);
            emit_qbe_operand(&instr->args[0], out);
            fprintf(out, ", ");
            emit_qbe_operand(&instr->args[1], out);
            fprintf(out, "\n\t");
            emit_qbe_operand(&instr->dest, out);
            fprintf(out, " =l extsw %%_cmp_%d", cmp_id);
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
            bool variadic = ir_call_is_variadic(prog, instr);
            if (instr->dest.type != OP_NONE) {
                emit_qbe_operand(&instr->dest, out);
                fprintf(out, " =%s ", qbe_itype(instr->dest_type));
            }
            fprintf(out, "call ");
            if (variadic) {
                // Variadic calls go through a fixed-arity stub (see stubs.c);
                // QBE's own '...' calling convention is not the Apple ABI.
                char *fname = emit_sanitize(instr->args[0].global);
                fprintf(out, "$%s_stub(", fname);
            } else {
                emit_qbe_operand(&instr->args[0], out);
                fprintf(out, "(");
            }

            // Always pass all 8 register args so the stub's stack shuffle
            // sees deterministic values; pad short variadic calls with zeros.
            int nargs = 0;
            for (int i = 1; i < 8 && instr->args[i].type != OP_NONE; i++) nargs++;
            int total = variadic ? 8 : nargs;
            for (int i = 0; i < total; i++) {
                if (i > 0) fprintf(out, ", ");
                fprintf(out, "l ");
                if (variadic && i >= nargs) {
                    fprintf(out, "0");
                } else {
                    emit_qbe_operand(&instr->args[i + 1], out);
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
    int len;
    const char *body = emit_unquote(value, &len);
    fprintf(out, "data $%s = { b \"%.*s\", b 0 }\n", name, len, body);
}

static void emit_qbe_function(QBEEmitter *e, IRFunction *fn, FILE *out, IRProgram *prog) {
    if (fn->is_public) fprintf(out, "export ");
    char *fname = emit_sanitize(fn->name);
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
            emit_qbe_instr(e, &b->instrs[j], out, prog);
            fprintf(out, "\n");
        }
    }

    fprintf(out, "}\n\n");
}

void emit_qbe(IRProgram *prog, FILE *out) {
    arena_reset(&g_arena_scratch);

    QBEEmitter emitter;
    emitter.cmp_counter = 0;
    ir_index_init(&emitter.index, prog, &g_arena_scratch);

    // Emit global data (skip struct type definitions)
    for (int i = 0; i < prog->num_globals; i++) {
        if (prog->globals[i].is_struct) continue; // skip struct type metadata
        if (prog->globals[i].is_extern) continue; // skip extern declarations (no data)
        char *gname = emit_sanitize(prog->globals[i].name);
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
        emit_qbe_function(&emitter, prog->functions[i], out, prog);
    }
}
