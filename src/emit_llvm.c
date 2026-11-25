#include "ir.h"
#include "arena.h"
#include "emit_common.h"

// LLVM IR emitter: IR → LLVM IR text
//
// Conventions:
//   All values are i64 (pointers too, per the IR's "everything is integer"
//   model). Pointer values cross boundaries with ptrtoint/inttoptr:
//     - IR_ADDR / IR_ALLOC produce i64 addresses via ptrtoint
//     - IR_LOAD / IR_STORE convert addresses with inttoptr
//     - extern C calls with pointer params convert args with inttoptr
//   llc (Mach-O target) adds the leading '_' to global symbols automatically.
//
// LLVM notes:
//   Opaque pointer type (LLVM 18+): load/store use bare 'ptr'.
//   No 'copy' instruction: IR_MOV becomes 'or i64 %x, 0' (folds to identity).
//   Comparisons produce i1; zext to i64.
//   Own (Clumsy) functions exchange i64 everywhere, even for pointer params;
//   pointer conversion only applies to extern C function calls.

typedef struct {
    FILE *out;
    int temp_counter;
    IRIndex index;
} LLVMEmitter;

// LLVM type for an IR value (pointers are addresses in our model)
static const char *llvm_value_type(IRType type) {
    switch (type) {
        case IR_TYPE_VOID: return "void";
        case IR_TYPE_PTR: return "i64";
        default: return "i64";
    }
}

static void emit_operand(IROperand *op, FILE *out) {
    switch (op->type) {
        case OP_NONE: break;
        case OP_IMM: fprintf(out, "%ld", (long)op->imm); break;
        case OP_REGISTER: fprintf(out, "%%%s", op->reg); break;
        case OP_VREG: fprintf(out, "%s", op->reg); break;
        case OP_BLOCK: {
            char *s = emit_sanitize_llvm(op->block);
            fprintf(out, "%%%s", s);
            break;
        }
        case OP_GLOBAL: {
            char *s = emit_sanitize_llvm(op->global);
            fprintf(out, "@%s", s);
            break;
        }
    }
}

// Emit "tmp = inttoptr i64 <op> to ptr" for an i64 address operand and return
// the temp name in 'buf'. For OP_GLOBAL, the pointer expression is emitted
// inline and buf is left empty (array/constant references decay to pointers).
static void emit_ptr_operand(LLVMEmitter *e, IROperand *op, char *buf, int buf_size) {
    FILE *out = e->out;
    if (op->type == OP_GLOBAL) {
        emit_operand(op, out);
        buf[0] = '\0';
        return;
    }
    snprintf(buf, buf_size, "%%_ll%d", e->temp_counter++);
    fprintf(out, "%s = inttoptr i64 ", buf);
    emit_operand(op, out);
    fprintf(out, " to ptr");
}

static const char *llvm_binop(IROpcode op) {
    switch (op) {
        case IR_ADD: return "add";
        case IR_SUB: return "sub";
        case IR_MUL: return "mul";
        case IR_DIV: return "sdiv";
        case IR_AND: return "and";
        case IR_OR: return "or";
        case IR_XOR: return "xor";
        case IR_SHL: return "shl";
        case IR_SHR: return "ashr";
        default: return "add";
    }
}

static const char *llvm_cmp_pred(IROpcode op) {
    switch (op) {
        case IR_EQ: return "eq";
        case IR_NE: return "ne";
        case IR_LT: return "slt";
        case IR_GT: return "sgt";
        case IR_LE: return "sle";
        case IR_GE: return "sge";
        default: return "eq";
    }
}

static void emit_llvm_instr(LLVMEmitter *e, IRInstr *instr, IRProgram *prog) {
    FILE *out = e->out;
    switch (instr->op) {
        case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV:
        case IR_AND: case IR_OR: case IR_XOR: case IR_SHL: case IR_SHR:
            fprintf(out, "  ");
            emit_operand(&instr->dest, out);
            fprintf(out, " = %s i64 ", llvm_binop(instr->op));
            emit_operand(&instr->args[0], out);
            fprintf(out, ", ");
            emit_operand(&instr->args[1], out);
            break;

        case IR_MOD:
            fprintf(out, "  ");
            emit_operand(&instr->dest, out);
            fprintf(out, " = srem i64 ");
            emit_operand(&instr->args[0], out);
            fprintf(out, ", ");
            emit_operand(&instr->args[1], out);
            break;

        case IR_NOT:
            fprintf(out, "  ");
            emit_operand(&instr->dest, out);
            fprintf(out, " = xor i64 ");
            emit_operand(&instr->args[0], out);
            fprintf(out, ", -1");
            break;

        case IR_NEG:
            fprintf(out, "  ");
            emit_operand(&instr->dest, out);
            fprintf(out, " = sub i64 0, ");
            emit_operand(&instr->args[0], out);
            break;

        case IR_EQ: case IR_NE: case IR_LT: case IR_GT: case IR_LE: case IR_GE: {
            char tn[32];
            snprintf(tn, sizeof(tn), "%%_ll%d", e->temp_counter++);
            fprintf(out, "  %s = icmp %s i64 ", tn, llvm_cmp_pred(instr->op));
            emit_operand(&instr->args[0], out);
            fprintf(out, ", ");
            emit_operand(&instr->args[1], out);
            fprintf(out, "\n  ");
            emit_operand(&instr->dest, out);
            fprintf(out, " = zext i1 %s to i64", tn);
            break;
        }

        case IR_LOAD: {
            char tn[32];
            fprintf(out, "  ");
            emit_ptr_operand(e, &instr->args[0], tn, sizeof(tn));
            fprintf(out, "\n  ");
            emit_operand(&instr->dest, out);
            fprintf(out, " = load i64, ptr %s", tn);
            break;
        }

        case IR_STORE: {
            char tn[32];
            fprintf(out, "  ");
            emit_ptr_operand(e, &instr->args[1], tn, sizeof(tn));
            fprintf(out, "\n  store i64 ");
            emit_operand(&instr->args[0], out);
            fprintf(out, ", ptr %s", tn);
            break;
        }

        case IR_ALLOC: {
            int alloc_size = 16;
            if (instr->args[0].type == OP_IMM) {
                alloc_size = (int)instr->args[0].imm;
                if (alloc_size <= 0) alloc_size = 16;
                else alloc_size = ((alloc_size + 15) / 16) * 16;
            }
            char tn[32];
            snprintf(tn, sizeof(tn), "%%_ll%d", e->temp_counter++);
            fprintf(out, "  %s = alloca [%d x i8], align 16\n", tn, alloc_size);
            fprintf(out, "  ");
            emit_operand(&instr->dest, out);
            fprintf(out, " = ptrtoint ptr %s to i64", tn);
            break;
        }

        case IR_JMP:
            fprintf(out, "  br label ");
            emit_operand(&instr->args[0], out);
            break;

        case IR_JNZ: {
            char tn[32];
            snprintf(tn, sizeof(tn), "%%_ll%d", e->temp_counter++);
            fprintf(out, "  %s = icmp ne i64 ", tn);
            emit_operand(&instr->args[0], out);
            fprintf(out, ", 0\n  br i1 %s, label ", tn);
            emit_operand(&instr->args[1], out);
            fprintf(out, ", label ");
            emit_operand(&instr->args[2], out);
            break;
        }

        case IR_RET:
            if (instr->args[0].type != OP_NONE) {
                fprintf(out, "  ret i64 ");
                emit_operand(&instr->args[0], out);
            } else {
                fprintf(out, "  ret void");
            }
            break;

        case IR_CALL: {
            const char *func_name = instr->args[0].global;
            int ex = ir_index_extern(&e->index, func_name);
            int fni = ir_index_function(&e->index, func_name);
            int nparams = 0;
            bool is_extern = false;

            if (ex >= 0) {
                is_extern = true;
                nparams = prog->externs[ex].num_params;
            } else if (fni >= 0) {
                // Own function: params exchange i64, never converted
                nparams = prog->functions[fni]->num_params;
            }

            // A variadic extern routes every call through its fixed-arity stub
            // (printf is always libc-variadic), so only the stub is declared.
            bool is_variadic = ir_extern_is_variadic(prog, func_name);

            int nargs = 0;
            for (int i = 1; i < 8 && instr->args[i].type != OP_NONE; i++) nargs++;

            // Self-consistent call type: fixed params must equal the actual
            // number of passed args (llc requires the call type to match its
            // own argument list). Stub calls are always fixed at 8 args.
            int fixed;
            if (is_variadic) {
                fixed = 8;
            } else if (is_extern) {
                fixed = nargs;
            } else {
                fixed = nparams;
            }

            // Result
            if (instr->dest.type != OP_NONE) {
                fprintf(out, "  ");
                emit_operand(&instr->dest, out);
                fprintf(out, " = call %s (", llvm_value_type(instr->dest_type));
            } else {
                fprintf(out, "  call %s (", llvm_value_type(instr->dest_type));
            }

            // Function type: fixed i64 params only (stub calls are fixed,
            // variadic callees are reached via the fixed-arity stub)
            for (int p = 0; p < fixed; p++) {
                if (p > 0) fprintf(out, ", ");
                fprintf(out, "i64");
            }
            fprintf(out, ") ");
            if (is_variadic) {
                fprintf(out, "@%s_stub(", func_name);
            } else {
                emit_operand(&instr->args[0], out);
                fprintf(out, "(");
            }

            // Actual args: i64 always. Global (ptr) operands convert with
            // ptrtoint (constant-expression form with parens). Stub calls pad
            // with zeros so x0-x7 are deterministic at the shim.
            for (int i = 0; i < fixed; i++) {
                if (i > 0) fprintf(out, ", ");
                fprintf(out, "i64 ");
                if (i >= nargs) {
                    fprintf(out, "0");
                } else if (instr->args[i + 1].type == OP_GLOBAL) {
                    fprintf(out, "ptrtoint (ptr ");
                    emit_operand(&instr->args[i + 1], out);
                    fprintf(out, " to i64)");
                } else {
                    emit_operand(&instr->args[i + 1], out);
                }
            }
            fprintf(out, ")");
            break;
        }

        case IR_PHI:
            fprintf(out, "  ");
            emit_operand(&instr->dest, out);
            fprintf(out, " = phi i64 ");
            for (int i = 0; i < instr->phi_count; i++) {
                if (i > 0) fprintf(out, ", ");
                fprintf(out, "[");
                emit_operand(&instr->phi_pairs[i * 2], out);
                fprintf(out, ", ");
                emit_operand(&instr->phi_pairs[i * 2 + 1], out);
                fprintf(out, "]");
            }
            break;

        case IR_MOV:
            if (instr->args[0].type == OP_GLOBAL) {
                // mov of a global address: ptrtoint to i64
                char tn[32];
                snprintf(tn, sizeof(tn), "%%_ll%d", e->temp_counter++);
                fprintf(out, "  %s = ptrtoint ptr ", tn);
                emit_operand(&instr->args[0], out);
                fprintf(out, " to i64\n  ");
                emit_operand(&instr->dest, out);
                fprintf(out, " = or i64 %s, 0", tn);
            } else {
                fprintf(out, "  ");
                emit_operand(&instr->dest, out);
                fprintf(out, " = or i64 ");
                emit_operand(&instr->args[0], out);
                fprintf(out, ", 0");
            }
            break;

        case IR_ADDR: {
            if (instr->args[0].type == OP_GLOBAL) {
                fprintf(out, "  ");
                emit_operand(&instr->dest, out);
                fprintf(out, " = ptrtoint ptr ");
                emit_operand(&instr->args[0], out);
                fprintf(out, " to i64");
            } else {
                char tn[32];
                snprintf(tn, sizeof(tn), "%%_ll%d", e->temp_counter++);
                fprintf(out, "  %s = inttoptr i64 ", tn);
                emit_operand(&instr->args[0], out);
                fprintf(out, " to ptr\n  ");
                emit_operand(&instr->dest, out);
                fprintf(out, " = ptrtoint ptr %s to i64", tn);
            }
            break;
        }
    }
}

static void emit_llvm_string_bytes(const char *value, FILE *out) {
    int len;
    const char *body = emit_unquote(value, &len);
    fprintf(out, "[");
    bool first = true;
    for (int i = 0; i < len;) {
        unsigned char byte;
        i = emit_decode_byte(body, len, i, &byte);
        if (!first) fprintf(out, ", ");
        first = false;
        fprintf(out, "i8 %u", (unsigned)byte);
    }
    if (!first) fprintf(out, ", ");
    fprintf(out, "i8 0]");
}

static void emit_llvm_string_global(const char *name, const char *value,
                                    const char *linkage, FILE *out) {
    char *n = emit_sanitize_llvm(name);
    fprintf(out, "@%s = %sconstant [%d x i8] ", n, linkage, emit_string_byte_count(value));
    emit_llvm_string_bytes(value, out);
    fprintf(out, "\n");
}

static void emit_llvm_function(LLVMEmitter *e, IRFunction *fn, IRProgram *prog) {
    FILE *out = e->out;
    fprintf(out, "define ");
    if (!fn->is_public) fprintf(out, "internal ");
    fprintf(out, "%s @", llvm_value_type(fn->return_type));
    {
        char *n = emit_sanitize_llvm(fn->name);
        fprintf(out, "%s", n);
    }
    fprintf(out, "(");
    for (int i = 0; i < fn->num_params; i++) {
        if (i > 0) fprintf(out, ", ");
        fprintf(out, "i64 %%%s", fn->params[i]);
    }
    fprintf(out, ") {\n");

    for (int i = 0; i < fn->num_blocks; i++) {
        IRBlock *b = fn->blocks[i];
        char *n = emit_sanitize_llvm(b->label);
        fprintf(out, "%s:\n", n);
        for (int j = 0; j < b->num_instrs; j++) {
            emit_llvm_instr(e, &b->instrs[j], prog);
            fprintf(out, "\n");
        }
    }
    fprintf(out, "}\n\n");
}

void emit_llvm(IRProgram *prog, FILE *out) {
    arena_reset(&g_arena_scratch);

    LLVMEmitter e = {0};
    e.out = out;
    ir_index_init(&e.index, prog, &g_arena_scratch);

    // Extern function declarations. All values are i64 in the IR model, so
    // externs are declared all-i64 regardless of their C signature (ARM64
    // passes i64/ptr identically in x-registers). Variadic externs are never
    // called directly: calls route through the fixed-arity <name>_stub shim
    // (see stubs.c), so only the stub needs declaring.
    bool printf_stub_declared = false;
    for (int i = 0; i < prog->num_externs; i++) {
        const char *name = prog->externs[i].name;
        int nparams = prog->externs[i].num_params;
        bool is_variadic = ir_extern_is_variadic(prog, name);

        if (is_variadic) {
            fprintf(out, "declare %s @%s_stub(i64, i64, i64, i64, i64, i64, i64, i64)\n",
                    llvm_value_type(prog->externs[i].return_type), name);
            if (strcmp(name, "printf") == 0) printf_stub_declared = true;
        } else {
            fprintf(out, "declare %s @%s(", llvm_value_type(prog->externs[i].return_type), name);
            for (int p = 0; p < nparams; p++) {
                if (p > 0) fprintf(out, ", ");
                fprintf(out, "i64");
            }
            fprintf(out, ")\n");
        }
    }

    // Undeclared printf (print/println builtin) needs its stub declared
    if (ir_index_extern(&e.index, "printf") < 0 && !printf_stub_declared) {
        bool uses_printf = false;
        for (int i = 0; i < prog->num_functions && !uses_printf; i++) {
            IRFunction *fn = prog->functions[i];
            for (int j = 0; j < fn->num_blocks && !uses_printf; j++) {
                IRBlock *b = fn->blocks[j];
                for (int k = 0; k < b->num_instrs && !uses_printf; k++) {
                    if (b->instrs[k].op == IR_CALL && b->instrs[k].args[0].type == OP_GLOBAL &&
                        strcmp(b->instrs[k].args[0].global, "printf") == 0) {
                        uses_printf = true;
                    }
                }
            }
        }
        if (uses_printf) {
            fprintf(out, "declare i64 @printf_stub(i64, i64, i64, i64, i64, i64, i64, i64)\n");
        }
    }
    if (prog->num_externs > 0 || ir_index_extern(&e.index, "printf") < 0) fprintf(out, "\n");

    // Global data (skip struct type metadata)
    for (int i = 0; i < prog->num_globals; i++) {
        if (prog->globals[i].is_struct) continue;
        char *gname = emit_sanitize_llvm(prog->globals[i].name);

        if (prog->globals[i].is_extern) {
            // Extern variable: emit a declaration only when this module does
            // not define it (cross-file reference). Same-module let+extern
            // (e.g. (let board ...) + (extern board ...)) must skip the decl,
            // else '@board' is redefined.
            bool defined_here = false;
            for (int k = 0; k < prog->num_globals; k++) {
                if (k != i && !prog->globals[k].is_extern &&
                    strcmp(prog->globals[k].name, prog->globals[i].name) == 0) {
                    defined_here = true;
                    break;
                }
            }
            if (defined_here) continue;
            fprintf(out, "@%s = external global i64\n", gname);
        } else if (prog->globals[i].is_string) {
            const char *linkage = prog->globals[i].is_public ? "" : "internal ";
            emit_llvm_string_global(gname, prog->globals[i].str_val, linkage, out);
        } else if (prog->globals[i].array_size > 0) {
            const char *linkage = prog->globals[i].is_public ? "" : "internal ";
            fprintf(out, "@%s = %sglobal [%d x i64] [", gname, linkage,
                    prog->globals[i].array_size);
            for (int j = 0; j < prog->globals[i].array_size; j++) {
                if (j > 0) fprintf(out, ", ");
                int64_t v = prog->globals[i].array_values
                                ? prog->globals[i].array_values[j]
                                : prog->globals[i].value;
                fprintf(out, "i64 %ld", (long)v);
            }
            fprintf(out, "]\n");
        } else {
            const char *linkage = prog->globals[i].is_public ? "" : "internal ";
            fprintf(out, "@%s = %sglobal i64 %ld\n", gname, linkage,
                    (long)prog->globals[i].value);
        }
    }
    if (prog->num_globals > 0) fprintf(out, "\n");

    // Function-local string literals
    int total_strings = 0;
    for (int i = 0; i < prog->num_functions; i++) {
        IRFunction *fn = prog->functions[i];
        total_strings += fn->num_strings;
        for (int j = 0; j < fn->num_strings; j++) {
            emit_llvm_string_global(fn->strings[j].label, fn->strings[j].value,
                                    "private unnamed_addr ", out);
        }
    }
    if (total_strings > 0) fprintf(out, "\n");

    // Functions
    for (int i = 0; i < prog->num_functions; i++) {
        emit_llvm_function(&e, prog->functions[i], prog);
    }
}
