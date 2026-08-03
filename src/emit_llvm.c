#include "ir.h"
#include <ctype.h>

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
} LLVMEmitter;

// Sanitize a name for LLVM: replace chars outside [a-zA-Z0-9_$.-] with '_'
static char *llvm_sanitize(const char *name) {
    int len = strlen(name);
    char *result = malloc(len + 1);
    for (int i = 0; i < len; i++) {
        char c = name[i];
        if (isalnum(c) || c == '_' || c == '$' || c == '.' || c == '-') {
            result[i] = c;
        } else {
            result[i] = '_';
        }
    }
    result[len] = '\0';
    return result;
}

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
            char *s = llvm_sanitize(op->block);
            fprintf(out, "%%%s", s);
            free(s);
            break;
        }
        case OP_GLOBAL: {
            char *s = llvm_sanitize(op->global);
            fprintf(out, "@%s", s);
            free(s);
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

// Resolve the callee signature for a call: index into prog->externs or
// prog->functions, or -1 if unknown.
static int find_extern(IRProgram *prog, const char *name) {
    for (int i = 0; i < prog->num_externs; i++) {
        if (strcmp(prog->externs[i].name, name) == 0) return i;
    }
    return -1;
}

static int find_function(IRProgram *prog, const char *name) {
    for (int i = 0; i < prog->num_functions; i++) {
        if (strcmp(prog->functions[i]->name, name) == 0) return i;
    }
    return -1;
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
            int ex = find_extern(prog, func_name);
            int fni = find_function(prog, func_name);
            int nparams = 0;
            bool is_variadic = false;
            bool is_extern = false;

            if (ex >= 0) {
                is_extern = true;
                nparams = prog->externs[ex].num_params;
                is_variadic = prog->externs[ex].is_variadic;
            } else if (fni >= 0) {
                // Own function: params exchange i64, never converted
                nparams = prog->functions[fni]->num_params;
            } else if (strcmp(func_name, "printf") == 0) {
                // Undeclared printf (print/println builtin): libc variadic
                is_extern = true;
                nparams = 1;
                is_variadic = true;
            }

            // Excess args beyond fixed params make an extern call variadic
            int nargs = 0;
            for (int i = 1; i < 8 && instr->args[i].type != OP_NONE; i++) nargs++;
            if (is_extern && nargs > nparams) is_variadic = true;

            // Self-consistent call type: fixed params must equal the actual
            // number of passed args (llc requires the call type to match its
            // own argument list). Variadic calls use the declared fixed count.
            int fixed;
            if (!is_extern) {
                fixed = nparams;
            } else if (is_variadic) {
                fixed = nparams;
            } else {
                fixed = nargs;
            }

            // Result
            if (instr->dest.type != OP_NONE) {
                fprintf(out, "  ");
                emit_operand(&instr->dest, out);
                fprintf(out, " = call %s (", llvm_value_type(instr->dest_type));
            } else {
                fprintf(out, "  call %s (", llvm_value_type(instr->dest_type));
            }

            // Function type: fixed i64 params, then '...' if variadic
            for (int p = 0; p < fixed; p++) {
                if (p > 0) fprintf(out, ", ");
                fprintf(out, "i64");
            }
            if (is_variadic) {
                if (fixed > 0) fprintf(out, ", ");
                fprintf(out, "...");
            }
            fprintf(out, ") ");
            emit_operand(&instr->args[0], out);
            fprintf(out, "(");

            // Actual args: i64 always. Global (ptr) operands convert with
            // ptrtoint (constant-expression form with parens). Explicit types
            // are required on variadic call args and harmless on fixed ones.
            for (int i = 1; i < 8 && instr->args[i].type != OP_NONE; i++) {
                if (i > 1) fprintf(out, ", ");
                fprintf(out, "i64 ");
                if (instr->args[i].type == OP_GLOBAL) {
                    fprintf(out, "ptrtoint (ptr ");
                    emit_operand(&instr->args[i], out);
                    fprintf(out, " to i64)");
                } else {
                    emit_operand(&instr->args[i], out);
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

// Decode the next byte at v[i]. If v[i] is the start of a C escape sequence
// (the IR stores source escapes verbatim, e.g. "\n" as two chars), decode it
// into one byte. Returns the index of the next byte.
static int decode_llvm_byte(const char *v, int len, int i, unsigned char *out) {
    unsigned char c = v[i];
    if (c == '\\' && i + 1 < len) {
        char e = v[i + 1];
        int next = i + 2;
        switch (e) {
            case 'n': *out = '\n'; return next;
            case 't': *out = '\t'; return next;
            case 'r': *out = '\r'; return next;
            case 'a': *out = '\a'; return next;
            case 'b': *out = '\b'; return next;
            case 'f': *out = '\f'; return next;
            case 'v': *out = '\v'; return next;
            case '\\': *out = '\\'; return next;
            case '"': *out = '"'; return next;
            case '\'': *out = '\''; return next;
            case 'x': {
                int val = 0, have = 0;
                while (next < len && isxdigit((unsigned char)v[next])) {
                    val = val * 16 + (isdigit((unsigned char)v[next])
                                          ? v[next] - '0'
                                          : tolower((unsigned char)v[next]) - 'a' + 10);
                    next++;
                    have = 1;
                }
                *out = have ? (unsigned char)val : 'x';
                return next;
            }
            default: {
                // Octal: up to 3 digits
                int val = 0, have = 0;
                if (e >= '0' && e <= '7') {
                    val = e - '0';
                    have = 1;
                    while (next < len && have < 3 && v[next] >= '0' && v[next] <= '7') {
                        val = val * 8 + (v[next] - '0');
                        next++;
                        have++;
                    }
                }
                *out = have ? (unsigned char)val : (unsigned char)e;
                return next;
            }
        }
    }
    *out = c;
    return i + 1;
}

// Byte count of a string after decoding C escapes, +1 for the NUL terminator.
static int decoded_string_byte_len(const char *value) {
    const char *v = value;
    int len = strlen(value);
    if (len >= 2 && v[0] == '"' && v[len - 1] == '"') {
        v++;
        len -= 2;
    }
    int n = 0;
    for (int i = 0; i < len;) {
        unsigned char byte;
        i = decode_llvm_byte(v, len, i, &byte);
        n++;
    }
    return n + 1;
}

// Emit a string literal as an explicit byte array. LLVM's c"..." escape
// support is limited (no \n/\t/\x, octal capped at 2 digits), so C escapes
// in the IR text are decoded to bytes and emitted as "i8 N" values.
static void emit_llvm_string_bytes(const char *value, FILE *out) {
    const char *v = value;
    int len = strlen(value);
    if (len >= 2 && v[0] == '"' && v[len - 1] == '"') {
        v++;
        len -= 2;
    }
    fprintf(out, "[");
    bool first = true;
    for (int i = 0; i < len;) {
        unsigned char byte;
        i = decode_llvm_byte(v, len, i, &byte);
        if (!first) fprintf(out, ", ");
        first = false;
        fprintf(out, "i8 %u", (unsigned)byte);
    }
    if (!first) fprintf(out, ", ");
    fprintf(out, "i8 0]");
}

static void emit_llvm_string_global(const char *name, const char *value,
                                    const char *linkage, FILE *out) {
    char *n = llvm_sanitize(name);
    fprintf(out, "@%s = %sconstant [%d x i8] ", n, linkage, decoded_string_byte_len(value));
    emit_llvm_string_bytes(value, out);
    fprintf(out, "\n");
    free(n);
}

static void emit_llvm_function(LLVMEmitter *e, IRFunction *fn, IRProgram *prog) {
    FILE *out = e->out;
    fprintf(out, "define ");
    if (!fn->is_public) fprintf(out, "internal ");
    fprintf(out, "%s @", llvm_value_type(fn->return_type));
    {
        char *n = llvm_sanitize(fn->name);
        fprintf(out, "%s", n);
        free(n);
    }
    fprintf(out, "(");
    for (int i = 0; i < fn->num_params; i++) {
        if (i > 0) fprintf(out, ", ");
        fprintf(out, "i64 %%%s", fn->params[i]);
    }
    fprintf(out, ") {\n");

    for (int i = 0; i < fn->num_blocks; i++) {
        IRBlock *b = fn->blocks[i];
        char *n = llvm_sanitize(b->label);
        fprintf(out, "%s:\n", n);
        free(n);
        for (int j = 0; j < b->num_instrs; j++) {
            emit_llvm_instr(e, &b->instrs[j], prog);
            fprintf(out, "\n");
        }
    }
    fprintf(out, "}\n\n");
}

void emit_llvm(IRProgram *prog, FILE *out) {
    // Extern function declarations. All values are i64 in the IR model, so
    // externs are declared all-i64 regardless of their C signature (ARM64
    // passes i64/ptr identically in x-registers).
    for (int i = 0; i < prog->num_externs; i++) {
        const char *name = prog->externs[i].name;
        int nparams = prog->externs[i].num_params;
        bool is_variadic = prog->externs[i].is_variadic;

        // A call site with more args than declared fixed params is variadic
        if (!is_variadic) {
            for (int f = 0; f < prog->num_functions && !is_variadic; f++) {
                IRFunction *fn = prog->functions[f];
                for (int b = 0; b < fn->num_blocks && !is_variadic; b++) {
                    IRBlock *blk = fn->blocks[b];
                    for (int k = 0; k < blk->num_instrs && !is_variadic; k++) {
                        if (blk->instrs[k].op == IR_CALL &&
                            blk->instrs[k].args[0].type == OP_GLOBAL &&
                            strcmp(blk->instrs[k].args[0].global, name) == 0) {
                            int nargs = 0;
                            for (int a = 1; a < 8 && blk->instrs[k].args[a].type != OP_NONE; a++)
                                nargs++;
                            if (nargs > nparams) is_variadic = true;
                        }
                    }
                }
            }
        }

        fprintf(out, "declare %s @%s(", llvm_value_type(prog->externs[i].return_type), name);
        for (int p = 0; p < nparams; p++) {
            if (p > 0) fprintf(out, ", ");
            fprintf(out, "i64");
        }
        if (is_variadic) {
            if (nparams > 0) fprintf(out, ", ");
            fprintf(out, "...");
        }
        fprintf(out, ")\n");
    }

    // Undeclared printf (print/println builtin) needs a libc declaration
    if (find_extern(prog, "printf") < 0) {
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
            fprintf(out, "declare i64 @printf(i64, ...)\n");
        }
    }
    if (prog->num_externs > 0 || find_extern(prog, "printf") < 0) fprintf(out, "\n");

    // Global data (skip struct type metadata)
    for (int i = 0; i < prog->num_globals; i++) {
        if (prog->globals[i].is_struct) continue;
        char *gname = llvm_sanitize(prog->globals[i].name);

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
            if (defined_here) {
                free(gname);
                continue;
            }
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
        free(gname);
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
    LLVMEmitter e = {out, 0};
    for (int i = 0; i < prog->num_functions; i++) {
        emit_llvm_function(&e, prog->functions[i], prog);
    }
}
