#include "ir.h"
#include "arena.h"
#include "emit_common.h"

// CLIF text emitter: post-SSA IR -> cranelift IR text (CLIF)
//
// The IR has already gone through cfg_ssa_pipeline, so register operands are
// either:
//   - OP_VREG named "%v_N" (N unique per function, assigned in dominator-tree
//     DFS order, so every definition precedes every use in that order), or
//   - OP_REGISTER named "pN" (function params; never renamed because they are
//     never defined).
//
// Value naming in the CLIF text (values must be named v<number>):
//   - vreg "%v_N"          -> vN  (already unique per function)
//   - param pN             -> v(param_base + N), param_base = max vreg + 1
//   - materialized temps   -> v(param_base + num_params + k)
//   - IR_MOV / IR_ADDR of a register (there is no 'copy' in CLIF) alias the
//     dest value name to the source value name via union-find; no instruction
//     is emitted for the alias.
//
// Because defs dominate uses (SSA) and blocks are emitted in dominator-tree
// DFS order (matching the rename order), every definition precedes its uses in
// the emitted text, which the cranelift reader requires.
//
// clifc (tools/clifc) resolves `colocated u0:N` to the Nth function declaration
// and `colocated u1:N` to the Nth data declaration, in file order. The emitter
// therefore places all empty-body extern function declarations first and emits
// the `;; data` section in the same order the gv references were assigned.

typedef struct DataRef {
    char *name;
    int id;
    char *str_val;       // for string literals
    int global_idx;      // index into prog->globals, or -1
} DataRef;

typedef struct {
    DataRef *items;
    int num;
    int cap;
} DataList;

typedef struct {
    FILE *out;
    IRProgram *prog;
    IRIndex index;

    int num_functions;
    int num_externs;

    // Implicit externs (undeclared call targets, e.g. printf)
    char **implicit;
    int num_implicit;
    int implicit_cap;

    // Variadic externs: calls need an x8-zeroing stub at link time (Apple AArch64)
    char **variadic;
    int num_variadic;
    int variadic_cap;

    // Global data list (name -> DataId), shared across functions
    DataList *dl;

    // The label of the block currently being emitted (phi-arg selection)
    const char *current_label;

    // Per-function value naming
    int param_base;        // first v-number used for function params
    int temp_counter;      // next v-number for materialized temps
    int num_vregs;         // max vreg number + 1
    int *alias;            // union-find over vreg numbers (MOV/ADDR aliases)
    int alias_cap;

    // Per-function blocks, in emission (dominator-DFS) order
    IRBlock **block_order;
    int *block_num;        // block_order index -> emitted blockN number
    int num_emitted;

    // Per-function explicit stack slots
    int *slot_sizes;       // ss index -> size
    int *alloc_vregs;      // ss index -> vreg number of its stack_addr value
    int num_slots;
    int slot_cap;
    int next_slot;

    // Per-function symbol references (preamble decls), first-reference order
    struct { char *name; int gv_idx; } *gv_refs;
    int num_gv_refs;
    int gv_refs_cap;
    int next_gv;
    struct { char *name; int fn_idx; } *fn_refs;
    int num_fn_refs;
    int fn_refs_cap;
    int next_fn;

    bool warned;
} ClifEmitter;

// --- small helpers ---

// Parse "%v_N" into N, or -1.
static int vreg_number(const char *name) {
    if (!name || strncmp(name, "%v_", 3) != 0) return -1;
    char *end = NULL;
    long n = strtol(name + 3, &end, 10);
    if (end == name + 3 || *end != '\0') return -1;
    return (int)n;
}



// Look up a string literal label (str_N) across all functions.
static const char *find_string_value(IRProgram *prog, const char *label) {
    for (int f = 0; f < prog->num_functions; f++) {
        IRFunction *fn = prog->functions[f];
        for (int i = 0; i < fn->num_strings; i++) {
            if (strcmp(fn->strings[i].label, label) == 0) return fn->strings[i].value;
        }
    }
    return NULL;
}

// --- global name classification / FuncId assignment ---

// FuncIds are assigned in file order: explicit externs, implicit externs
// (call targets with no declaration, e.g. printf from print/println), then
// own functions.
static int func_id_for(ClifEmitter *e, const char *name) {
    int ei = ir_index_extern(&e->index, name);
    if (ei >= 0) return ei;
    int fi = ir_index_function(&e->index, name);
    if (fi >= 0) return e->prog->num_externs + e->num_implicit + fi;
    // implicit externs appended after explicit ones, in first-use order
    for (int i = 0; i < e->num_implicit; i++) {
        if (strcmp(e->implicit[i], name) == 0) return e->prog->num_externs + i;
    }
    if (e->num_implicit >= e->implicit_cap) {
        int old_cap = e->implicit_cap;
        e->implicit_cap *= 2;
        e->implicit = arena_grow(&g_arena_scratch, e->implicit,
                                 (size_t)old_cap * sizeof(char *),
                                 (size_t)e->implicit_cap * sizeof(char *));
    }
    e->implicit[e->num_implicit] = ir_strdup(&g_arena_scratch, name);
    return e->prog->num_externs + e->num_implicit++;
}

// Classify an OP_GLOBAL name as a function reference (returns FuncId, sets
// *is_func) or data reference.
static int classify_global(ClifEmitter *e, const char *name, bool *is_func) {
    if (ir_index_extern(&e->index, name) >= 0 ||
        ir_index_function(&e->index, name) >= 0) {
        *is_func = true;
        return func_id_for(e, name);
    }
    *is_func = false;
    if (find_string_value(e->prog, name)) return -1; // string data (no global idx)
    return ir_index_global(&e->index, name);
}

// --- data collection (name -> DataId) ---

static void mark_variadic(ClifEmitter *e, const char *name) {
    for (int i = 0; i < e->num_variadic; i++) {
        if (strcmp(e->variadic[i], name) == 0) return;
    }
    if (e->num_variadic >= e->variadic_cap) {
        int old_cap = e->variadic_cap;
        e->variadic_cap *= 2;
        e->variadic = arena_grow(&g_arena_scratch, e->variadic,
                                 (size_t)old_cap * sizeof(char *),
                                 (size_t)e->variadic_cap * sizeof(char *));
    }
    e->variadic[e->num_variadic] = ir_strdup(&g_arena_scratch, name);
    e->num_variadic++;
}

// Mark a call target as needing an x8-zeroing stub: either the call is flagged
// variadic (print/println), the extern was declared variadic, or more args are
// passed than the extern declares (printf called through a non-variadic decl).
// The rule is shared with the qbe/llvm emitters (ir_call_is_variadic).
static void mark_call_variadic(ClifEmitter *e, IRInstr *in) {
    if (ir_call_is_variadic(e->prog, in)) {
        mark_variadic(e, in->args[0].global);
    }
}

static void data_add(DataList *dl, const char *name, int global_idx, const char *str_val) {
    for (int i = 0; i < dl->num; i++) {
        if (strcmp(dl->items[i].name, name) == 0) return;
    }
    if (dl->num >= dl->cap) {
        int old_cap = dl->cap;
        dl->cap *= 2;
        dl->items = arena_grow(&g_arena_scratch, dl->items,
                               (size_t)old_cap * sizeof(DataRef),
                               (size_t)dl->cap * sizeof(DataRef));
    }
    dl->items[dl->num].name = ir_strdup(&g_arena_scratch, name);
    dl->items[dl->num].id = dl->num;
    dl->items[dl->num].global_idx = global_idx;
    dl->items[dl->num].str_val = str_val ? ir_strdup(&g_arena_scratch, str_val) : NULL;
    dl->num++;
}

// --- pre-scan pass 1: collect data references, implicit externs ---

static void collect_global_ref(ClifEmitter *e, DataList *dl, const char *name) {
    bool is_func;
    int id = classify_global(e, name, &is_func);
    if (is_func) {
        func_id_for(e, name); // ensure implicit extern registered
        return;
    }
    const char *sv = (id < 0) ? find_string_value(e->prog, name) : NULL;
    if (id >= 0 || sv) {
        data_add(dl, name, id, sv);
        return;
    }
    // Unknown global: treat as string data, warn at emission time
    data_add(dl, name, -1, NULL);
}

static void pre_scan_refs(ClifEmitter *e, DataList *dl) {
    IRProgram *prog = e->prog;

    // Data-only files (globals with no functions, e.g. config.cpl) never hit
    // the function-body scan below, so their definitions would be lost. Emit
    // every global defined in this file up front; data_add dedups by name, so
    // referenced globals keep their first-assigned ids.
    for (int i = 0; i < prog->num_globals; i++) {
        if (!prog->globals[i].is_extern) {
            data_add(dl, prog->globals[i].name, i, NULL);
        }
    }

    for (int f = 0; f < prog->num_functions; f++) {
        IRFunction *fn = prog->functions[f];
        for (int b = 0; b < fn->num_blocks; b++) {
            IRBlock *blk = fn->blocks[b];
            for (int i = 0; i < blk->num_instrs; i++) {
                IRInstr *in = &blk->instrs[i];
                if (in->op == IR_CALL && in->args[0].type == OP_GLOBAL) {
                    func_id_for(e, in->args[0].global); // registers implicit extern
                    mark_call_variadic(e, in);
                }
                for (int a = (in->op == IR_CALL) ? 1 : 0; a < 8; a++) {
                    if (in->args[a].type == OP_GLOBAL) {
                        collect_global_ref(e, dl, in->args[a].global);
                    }
                }
                for (int p = 0; p < in->phi_count; p++) {
                    IROperand *val = &in->phi_pairs[p * 2];
                    if (val->type == OP_GLOBAL) {
                        collect_global_ref(e, dl, val->global);
                    }
                }
            }
        }
    }
}

static int string_bytes(const char *value, unsigned char *buf, int buf_cap) {
    return emit_string_bytes(value, buf, buf_cap);
}

// --- per-function value/block pre-scan ---

// Recursively collect blocks in dominator-tree DFS order (matches the SSA
// rename order, guaranteeing def-before-use in the emitted text).
static void dfs_blocks(ClifEmitter *e, IRBlock *b, IRFunction *fn) {
    int idx = -1;
    for (int i = 0; i < fn->num_blocks; i++) {
        if (fn->blocks[i] == b) { idx = i; break; }
    }
    if (idx < 0) return;
    if (e->block_num[idx] >= 0) return;

    e->block_num[idx] = e->num_emitted;
    e->block_order[e->num_emitted++] = b;

    for (int c = 0; c < b->num_dom_children; c++) {
        dfs_blocks(e, b->dom_children[c], fn);
    }
}

static void scan_vreg(ClifEmitter *e, IROperand *op) {
    if (op->type != OP_VREG) return;
    int n = vreg_number(op->reg);
    if (n >= 0 && n >= e->num_vregs) e->num_vregs = n + 1;
}

static void scan_operand_refs(ClifEmitter *e, IROperand *op, bool force_func) {
    if (op->type == OP_GLOBAL) {
        bool is_func;
        int id = classify_global(e, op->global, &is_func);
        if (is_func || force_func) {
            func_id_for(e, op->global); // register implicit extern if needed
            for (int i = 0; i < e->num_fn_refs; i++) {
                if (strcmp(e->fn_refs[i].name, op->global) == 0) return;
            }
            if (e->num_fn_refs >= e->fn_refs_cap) {
                int old_cap = e->fn_refs_cap;
                e->fn_refs_cap *= 2;
                e->fn_refs = arena_grow(&g_arena_scratch, e->fn_refs,
                                        (size_t)old_cap * sizeof(e->fn_refs[0]),
                                        (size_t)e->fn_refs_cap * sizeof(e->fn_refs[0]));
            }
            e->fn_refs[e->num_fn_refs].name = ir_strdup(&g_arena_scratch, op->global);
            e->fn_refs[e->num_fn_refs].fn_idx = e->next_fn++;
            e->num_fn_refs++;
            (void)id;
        } else {
            for (int i = 0; i < e->num_gv_refs; i++) {
                if (strcmp(e->gv_refs[i].name, op->global) == 0) return;
            }
            if (e->num_gv_refs >= e->gv_refs_cap) {
                int old_cap = e->gv_refs_cap;
                e->gv_refs_cap *= 2;
                e->gv_refs = arena_grow(&g_arena_scratch, e->gv_refs,
                                        (size_t)old_cap * sizeof(e->gv_refs[0]),
                                        (size_t)e->gv_refs_cap * sizeof(e->gv_refs[0]));
            }
            e->gv_refs[e->num_gv_refs].name = ir_strdup(&g_arena_scratch, op->global);
            e->gv_refs[e->num_gv_refs].gv_idx = e->next_gv++;
            e->num_gv_refs++;
        }
    }
}

static void pre_scan_function(ClifEmitter *e, IRFunction *fn) {
    // Block emission order + numbers
    e->num_emitted = 0;
    for (int i = 0; i < fn->num_blocks; i++) e->block_num[i] = -1;
    dfs_blocks(e, fn->entry, fn);
    // Defensive: any block not reachable through the dominator tree is
    // appended at the end so it still gets emitted.
    for (int i = 0; i < fn->num_blocks; i++) {
        if (e->block_num[i] < 0) {
            e->block_num[i] = e->num_emitted;
            e->block_order[e->num_emitted++] = fn->blocks[i];
        }
    }

    // Reset per-function naming state
    e->num_vregs = 0;
    e->num_slots = 0;
    e->next_slot = 0;
    e->num_gv_refs = 0;
    e->next_gv = 0;
    e->num_fn_refs = 0;
    e->next_fn = 0;

    for (int i = 0; i < e->num_emitted; i++) {
        IRBlock *b = e->block_order[i];
        for (int j = 0; j < b->num_instrs; j++) {
            IRInstr *in = &b->instrs[j];

            scan_vreg(e, &in->dest);
            for (int a = 0; a < 8; a++) scan_vreg(e, &in->args[a]);
            for (int p = 0; p < in->phi_count; p++) {
                scan_vreg(e, &in->phi_pairs[p * 2]);
                scan_vreg(e, &in->phi_pairs[p * 2 + 1]);
            }

            // Collect symbol references (skip phi pairs — never global)
            scan_operand_refs(e, &in->dest, false);
            for (int a = 0; a < 8; a++) {
                scan_operand_refs(e, &in->args[a], in->op == IR_CALL && a == 0);
            }

            if (in->op == IR_ALLOC) {
                int size = 16;
                if (in->args[0].type == OP_IMM) {
                    size = (int)in->args[0].imm;
                    if (size <= 0) size = 16;
                    size = ((size + 15) / 16) * 16;
                }
                if (e->num_slots >= e->slot_cap) {
                    int old_cap = e->slot_cap;
                    e->slot_cap *= 2;
                    e->slot_sizes = arena_grow(&g_arena_scratch, e->slot_sizes,
                                               (size_t)old_cap * sizeof(int),
                                               (size_t)e->slot_cap * sizeof(int));
                    e->alloc_vregs = arena_grow(&g_arena_scratch, e->alloc_vregs,
                                                (size_t)old_cap * sizeof(int),
                                                (size_t)e->slot_cap * sizeof(int));
                }
                e->slot_sizes[e->num_slots] = size;
                e->alloc_vregs[e->num_slots] = vreg_number(in->dest.reg);
                e->num_slots++;
            }
        }
    }

    e->param_base = e->num_vregs + 1;
    e->temp_counter = e->param_base + fn->num_params;

    // Alias table for MOV/ADDR register copies
    if (e->num_vregs > 0) {
        if (e->num_vregs > e->alias_cap) {
            e->alias = arena_grow(&g_arena_scratch, e->alias,
                                  (size_t)e->alias_cap * sizeof(int),
                                  (size_t)e->num_vregs * sizeof(int));
            e->alias_cap = e->num_vregs;
        }
        for (int i = 0; i < e->num_vregs; i++) e->alias[i] = i;
    }
}

// --- emission helpers ---

static int find_root(ClifEmitter *e, int n) {
    while (e->alias[n] != n) {
        e->alias[n] = e->alias[e->alias[n]];
        n = e->alias[n];
    }
    return n;
}

static int block_num_for(ClifEmitter *e, IRFunction *fn, const char *label) {
    for (int i = 0; i < fn->num_blocks; i++) {
        if (strcmp(fn->blocks[i]->label, label) == 0) return e->block_num[i];
    }
    return -1;
}

static IRBlock *block_for(IRFunction *fn, const char *label) {
    for (int i = 0; i < fn->num_blocks; i++) {
        if (strcmp(fn->blocks[i]->label, label) == 0) return fn->blocks[i];
    }
    return NULL;
}

// Look up the DataId for a data symbol name.
static int data_id_for(ClifEmitter *e, const char *name) {
    for (int i = 0; i < e->dl->num; i++) {
        if (strcmp(e->dl->items[i].name, name) == 0) return e->dl->items[i].id;
    }
    return 0;
}

// Look up the gv index for a data symbol in this function's preamble refs.
static int gv_for(ClifEmitter *e, const char *name) {
    for (int i = 0; i < e->num_gv_refs; i++) {
        if (strcmp(e->gv_refs[i].name, name) == 0) return e->gv_refs[i].gv_idx;
    }
    return -1;
}

static int fn_for(ClifEmitter *e, const char *name) {
    for (int i = 0; i < e->num_fn_refs; i++) {
        if (strcmp(e->fn_refs[i].name, name) == 0) return e->fn_refs[i].fn_idx;
    }
    return -1;
}

// Resolve an operand to a CLIF value name, materializing constants/globals
// into fresh v-registers (each with its own instruction line).
static void clif_operand(ClifEmitter *e, IROperand *op, char *buf, size_t n) {
    switch (op->type) {
        case OP_VREG: {
            int num = vreg_number(op->reg);
            if (num >= 0 && num < e->num_vregs) {
                snprintf(buf, n, "v%d", find_root(e, num));
            } else {
                if (!e->warned) {
                    fprintf(stderr, "warning: undefined vreg '%s' -> 0\n", op->reg);
                    e->warned = true;
                }
                int t = e->temp_counter++;
                fprintf(e->out, "    v%d = iconst.i64 0\n", t);
                snprintf(buf, n, "v%d", t);
            }
            break;
        }
        case OP_REGISTER: {
            // pN (function param) survives the SSA rename untouched
            int num = -1;
            if (strncmp(op->reg, "p", 1) == 0) {
                char *end = NULL;
                long x = strtol(op->reg + 1, &end, 10);
                if (end != op->reg + 1 && *end == '\0') num = (int)x;
            }
            if (num >= 0) {
                snprintf(buf, n, "v%d", e->param_base + num);
            } else {
                if (!e->warned) {
                    fprintf(stderr, "warning: undefined register '%s' -> 0\n", op->reg);
                    e->warned = true;
                }
                int t = e->temp_counter++;
                fprintf(e->out, "    v%d = iconst.i64 0\n", t);
                snprintf(buf, n, "v%d", t);
            }
            break;
        }
        case OP_IMM: {
            int t = e->temp_counter++;
            fprintf(e->out, "    v%d = iconst.i64 %ld\n", t, (long)op->imm);
            snprintf(buf, n, "v%d", t);
            break;
        }
        case OP_GLOBAL: {
            int gv = gv_for(e, op->global);
            if (gv >= 0) {
                int t = e->temp_counter++;
                fprintf(e->out, "    v%d = symbol_value.i64 gv%d\n", t, gv);
                snprintf(buf, n, "v%d", t);
            } else {
                int fn = fn_for(e, op->global);
                if (fn >= 0) {
                    int t = e->temp_counter++;
                    fprintf(e->out, "    v%d = func_addr.i64 fn%d\n", t, fn);
                    snprintf(buf, n, "v%d", t);
                } else {
                    if (!e->warned) {
                        fprintf(stderr, "warning: unknown global '%s' -> 0\n", op->global);
                        e->warned = true;
                    }
                    int t = e->temp_counter++;
                    fprintf(e->out, "    v%d = iconst.i64 0\n", t);
                    snprintf(buf, n, "v%d", t);
                }
            }
            break;
        }
        case OP_NONE: {
            int t = e->temp_counter++;
            fprintf(e->out, "    v%d = iconst.i64 0\n", t);
            snprintf(buf, n, "v%d", t);
            break;
        }
        default:
            snprintf(buf, n, "v0");
            break;
    }
}

// Append the block-parameter arguments for a jump to `target` coming from
// `pred` (phi pair values matching the predecessor label), then close the
// paren if any were emitted.
static void emit_branch_args(ClifEmitter *e, IRFunction *fn,
                             const char *target_label, const char *pred_label) {
    IRBlock *target = block_for(fn, target_label);
    if (!target) return;
    bool first = true;
    for (int i = 0; i < target->num_instrs; i++) {
        IRInstr *in = &target->instrs[i];
        if (in->op != IR_PHI) break;
        for (int p = 0; p < in->phi_count; p++) {
            if (in->phi_pairs[p * 2 + 1].type == OP_BLOCK &&
                strcmp(in->phi_pairs[p * 2 + 1].block, pred_label) == 0) {
                char val[32];
                clif_operand(e, &in->phi_pairs[p * 2], val, sizeof(val));
                if (first) {
                    fprintf(e->out, "(");
                    first = false;
                } else {
                    fprintf(e->out, ", ");
                }
                fprintf(e->out, "%s", val);
            }
        }
    }
    if (!first) fprintf(e->out, ")");
}

static const char *clif_binop(IROpcode op) {
    switch (op) {
        case IR_ADD: return "iadd";
        case IR_SUB: return "isub";
        case IR_MUL: return "imul";
        case IR_DIV: return "sdiv";
        case IR_MOD: return "srem";
        case IR_AND: return "band";
        case IR_OR: return "bor";
        case IR_XOR: return "bxor";
        case IR_SHL: return "ishl";
        case IR_SHR: return "ushr";
        default: return "iadd";
    }
}

static const char *clif_cmp(IROpcode op) {
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

static IRType callee_return(ClifEmitter *e, const char *name) {
    int ei = ir_index_extern(&e->index, name);
    if (ei >= 0) return e->prog->externs[ei].return_type;
    int fi = ir_index_function(&e->index, name);
    if (fi >= 0) return e->prog->functions[fi]->return_type;
    return IR_TYPE_I64;
}

// Number of i64 parameters the callee's CLIF signature declares.
static int callee_param_count(ClifEmitter *e, const char *name) {
    int ei = ir_index_extern(&e->index, name);
    if (ei >= 0) return 8; // all externs declared with 8 fixed i64 params
    int fi = ir_index_function(&e->index, name);
    if (fi >= 0) return e->prog->functions[fi]->num_params;
    return 8;
}

static void emit_sig_params(FILE *out, int nparams) {
    for (int i = 0; i < nparams; i++) {
        if (i > 0) fprintf(out, ", ");
        fprintf(out, "i64");
    }
}

static void emit_clif_instr(ClifEmitter *e, IRFunction *fn, IRInstr *in) {
    FILE *out = e->out;

    switch (in->op) {
        case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV:
        case IR_MOD: case IR_AND: case IR_OR: case IR_XOR:
        case IR_SHL: case IR_SHR: {
            char a[32], b[32];
            clif_operand(e, &in->args[0], a, sizeof(a));
            clif_operand(e, &in->args[1], b, sizeof(b));
            fprintf(out, "    v%d = %s %s, %s\n",
                    vreg_number(in->dest.reg), clif_binop(in->op), a, b);
            break;
        }

        case IR_NOT: {
            char a[32];
            clif_operand(e, &in->args[0], a, sizeof(a));
            fprintf(out, "    v%d = bnot %s\n", vreg_number(in->dest.reg), a);
            break;
        }

        case IR_NEG: {
            char a[32];
            clif_operand(e, &in->args[0], a, sizeof(a));
            fprintf(out, "    v%d = ineg %s\n", vreg_number(in->dest.reg), a);
            break;
        }

        case IR_EQ: case IR_NE: case IR_LT: case IR_GT: case IR_LE: case IR_GE: {
            char a[32], b[32];
            clif_operand(e, &in->args[0], a, sizeof(a));
            clif_operand(e, &in->args[1], b, sizeof(b));
            int cmp_t = e->temp_counter++;
            fprintf(out, "    v%d = icmp.i64 %s %s, %s\n", cmp_t, clif_cmp(in->op), a, b);
            fprintf(out, "    v%d = uextend.i64 v%d\n", vreg_number(in->dest.reg), cmp_t);
            break;
        }

        case IR_LOAD: {
            char p[32];
            clif_operand(e, &in->args[0], p, sizeof(p));
            fprintf(out, "    v%d = load.i64 %s\n", vreg_number(in->dest.reg), p);
            break;
        }

        case IR_STORE: {
            char v[32], p[32];
            clif_operand(e, &in->args[0], v, sizeof(v));
            clif_operand(e, &in->args[1], p, sizeof(p));
            fprintf(out, "    store.i64 %s, %s\n", v, p);
            break;
        }

        case IR_ALLOC: {
            // Hoisted to the entry block (see emit_cranelift_function)
            break;
        }

        case IR_JMP: {
            fprintf(out, "    jump block%d", block_num_for(e, fn, in->args[0].block));
            emit_branch_args(e, fn, in->args[0].block, e->current_label);
            fprintf(out, "\n");
            break;
        }

        case IR_JNZ: {
            char cond[32];
            clif_operand(e, &in->args[0], cond, sizeof(cond));
            fprintf(out, "    brif %s, block%d", cond,
                    block_num_for(e, fn, in->args[1].block));
            emit_branch_args(e, fn, in->args[1].block, e->current_label);
            fprintf(out, ", block%d", block_num_for(e, fn, in->args[2].block));
            emit_branch_args(e, fn, in->args[2].block, e->current_label);
            fprintf(out, "\n");
            break;
        }

        case IR_RET:
            if (in->args[0].type != OP_NONE) {
                char v[32];
                clif_operand(e, &in->args[0], v, sizeof(v));
                fprintf(out, "    return %s\n", v);
            } else {
                fprintf(out, "    return\n");
            }
            break;

        case IR_CALL: {
            const char *fname = in->args[0].global;
            int nparams = callee_param_count(e, fname);
            IRType ret = callee_return(e, fname);
            int fid = func_id_for(e, fname);

            int actual = 0;
            for (int i = 1; i < 8 && in->args[i].type != OP_NONE; i++) actual++;

            char names[8][32];
            for (int k = 0; k < nparams && k < 8; k++) {
                IROperand op = op_none();
                if (k < actual) op = in->args[k + 1];
                clif_operand(e, &op, names[k], sizeof(names[k]));
            }

            int dest = -1;
            int fnr = fn_for(e, fname);
            if (fnr < 0) fnr = fid; // defensive fallback
            if (ret != IR_TYPE_VOID) {
                if (in->dest.type == OP_NONE) {
                    dest = e->temp_counter++;
                } else {
                    dest = vreg_number(in->dest.reg);
                }
                fprintf(out, "    v%d = call fn%d(", dest, fnr);
            } else {
                fprintf(out, "    call fn%d(", fnr);
            }
            for (int k = 0; k < nparams && k < 8; k++) {
                if (k > 0) fprintf(out, ", ");
                fprintf(out, "%s", names[k]);
            }
            fprintf(out, ")\n");
            break;
        }

        case IR_MOV:
            if (in->args[0].type == OP_GLOBAL) {
                // mov of a symbol address: emit the symbol reference directly
                // into the dest value.
                int gv = gv_for(e, in->args[0].global);
                if (gv >= 0) {
                    fprintf(out, "    v%d = symbol_value.i64 gv%d\n",
                            vreg_number(in->dest.reg), gv);
                } else {
                    int fnr = fn_for(e, in->args[0].global);
                    if (fnr >= 0) {
                        fprintf(out, "    v%d = func_addr.i64 fn%d\n",
                                vreg_number(in->dest.reg), fnr);
                    } else {
                        fprintf(out, "    v%d = iconst.i64 0\n",
                                vreg_number(in->dest.reg));
                    }
                }
            } else if (in->args[0].type == OP_IMM) {
                fprintf(out, "    v%d = iconst.i64 %ld\n",
                        vreg_number(in->dest.reg), (long)in->args[0].imm);
            } else if (in->args[0].type == OP_REGISTER ||
                       in->args[0].type == OP_VREG) {
                // No 'copy' in CLIF: alias the dest name to the source name.
                int src = in->args[0].type == OP_VREG
                              ? vreg_number(in->args[0].reg)
                              : e->param_base + atoi(in->args[0].reg + 1);
                int d = vreg_number(in->dest.reg);
                if (d >= 0 && src >= 0 && d < e->num_vregs) {
                    e->alias[d] = find_root(e, src);
                }
            } else {
                fprintf(out, "    v%d = iconst.i64 0\n", vreg_number(in->dest.reg));
            }
            break;

        case IR_ADDR:
            if (in->args[0].type == OP_GLOBAL) {
                int gv = gv_for(e, in->args[0].global);
                if (gv >= 0) {
                    fprintf(out, "    v%d = symbol_value.i64 gv%d\n",
                            vreg_number(in->dest.reg), gv);
                } else {
                    int fnr = fn_for(e, in->args[0].global);
                    if (fnr >= 0) {
                        fprintf(out, "    v%d = func_addr.i64 fn%d\n",
                                vreg_number(in->dest.reg), fnr);
                    } else {
                        fprintf(out, "    v%d = iconst.i64 0\n",
                                vreg_number(in->dest.reg));
                    }
                }
            } else if (in->args[0].type == OP_REGISTER ||
                       in->args[0].type == OP_VREG) {
                int src = in->args[0].type == OP_VREG
                              ? vreg_number(in->args[0].reg)
                              : e->param_base + atoi(in->args[0].reg + 1);
                int d = vreg_number(in->dest.reg);
                if (d >= 0 && src >= 0 && d < e->num_vregs) {
                    e->alias[d] = find_root(e, src);
                }
            } else {
                fprintf(out, "    v%d = iconst.i64 0\n", vreg_number(in->dest.reg));
            }
            break;

        case IR_PHI:
            // Handled as block parameters
            break;
    }
}

// --- function emission ---

static void emit_extern_decl(FILE *out, const char *name, IRType ret, int nparams,
                             bool stub) {
    char *n = emit_sanitize(name);
    fprintf(out, "function %%%s", n);
    if (stub) fprintf(out, "_stub");
    fprintf(out, "(");
    emit_sig_params(out, nparams);
    fprintf(out, ")");
    if (ret != IR_TYPE_VOID) fprintf(out, " -> i64");
    fprintf(out, " {}\n");
}

static void emit_cranelift_function(ClifEmitter *e, IRFunction *fn) {
    FILE *out = e->out;

    pre_scan_function(e, fn);

    char *fname = emit_sanitize(fn->name);
    fprintf(out, "function %%%s(", fname);
    emit_sig_params(out, fn->num_params);
    fprintf(out, ")");
    if (fn->return_type != IR_TYPE_VOID) fprintf(out, " -> i64");
    fprintf(out, " {\n");

    // Preamble: data and function references, then explicit stack slots
    for (int i = 0; i < e->num_gv_refs; i++) {
        fprintf(out, "    gv%d = symbol colocated u1:%d\n",
                e->gv_refs[i].gv_idx, data_id_for(e, e->gv_refs[i].name));
    }
    for (int i = 0; i < e->num_fn_refs; i++) {
        int fid = func_id_for(e, e->fn_refs[i].name);
        const char *callee = e->fn_refs[i].name;
        int nparams = callee_param_count(e, callee);
        IRType ret = callee_return(e, callee);
        fprintf(out, "    fn%d = colocated u0:%d (", e->fn_refs[i].fn_idx, fid);
        emit_sig_params(out, nparams);
        fprintf(out, ")");
        if (ret != IR_TYPE_VOID) fprintf(out, " -> i64");
        fprintf(out, "\n");
    }
    for (int i = 0; i < e->num_slots; i++) {
        fprintf(out, "    ss%d = explicit_slot %d\n", i, e->slot_sizes[i]);
    }

    e->next_slot = 0;
    for (int i = 0; i < e->num_emitted; i++) {
        IRBlock *b = e->block_order[i];
        int bidx = -1;
        for (int bi = 0; bi < fn->num_blocks; bi++) {
            if (fn->blocks[bi] == b) { bidx = bi; break; }
        }
        if (i == 0) {
            // Entry block params must match the function signature
            fprintf(out, "block%d(", e->block_num[bidx]);
            for (int p = 0; p < fn->num_params; p++) {
                if (p > 0) fprintf(out, ", ");
                fprintf(out, "v%d: i64", e->param_base + p);
            }
            fprintf(out, "):\n");
        } else {
            int nparams = 0;
            for (int j = 0; j < b->num_instrs; j++) {
                if (b->instrs[j].op != IR_PHI) break;
                nparams++;
            }
            fprintf(out, "block%d(", e->block_num[bidx]);
            for (int p = 0; p < nparams; p++) {
                if (p > 0) fprintf(out, ", ");
                fprintf(out, "v%d: i64", vreg_number(b->instrs[p].dest.reg));
            }
            fprintf(out, "):\n");
        }
        // Hoist all stack_addr values to the entry block: the IR materializes a
        // variable's slot address in whichever block first touches it, but other
        // blocks (reached without going through that block) may also load/store
        // the slot. In the entry block the address dominates every use, which
        // cranelift's strict-dominance verifier requires (and QBE tolerates).
        if (i == 0) {
            for (int s = 0; s < e->num_slots; s++) {
                if (e->alloc_vregs[s] >= 0)
                    fprintf(out, "    v%d = stack_addr.i64 ss%d\n", e->alloc_vregs[s], s);
            }
        }
        // Track the label for phi-arg selection at branch sites
        e->current_label = b->label;
        for (int j = 0; j < b->num_instrs; j++) {
            if (b->instrs[j].op == IR_PHI) continue;
            emit_clif_instr(e, fn, &b->instrs[j]);
        }
    }
    fprintf(out, "}\n\n");
}

// --- data emission ---

static void emit_data_section(ClifEmitter *e, DataList *dl) {
    if (dl->num == 0) return;
    FILE *out = e->out;
    fprintf(out, ";; data\n");
    for (int i = 0; i < dl->num; i++) {
        DataRef *d = &dl->items[i];
        char *n = emit_sanitize(d->name);

        const char *linkage = "local";
        bool writable = false;
        bool defined = false;

        // Size the buffer to the item (strings/arrays can exceed any fixed cap)
        int cap = 8;
        if (d->str_val) {
            cap = (int)strlen(d->str_val) + 1;
        } else if (d->global_idx >= 0 && d->global_idx < e->prog->num_globals) {
            int gi = d->global_idx;
            if (e->prog->globals[gi].is_string) cap = e->prog->globals[gi].str_val ? (int)strlen(e->prog->globals[gi].str_val) + 1 : 8;
            else if (e->prog->globals[gi].array_size > 0) cap = e->prog->globals[gi].array_size * 8;
            else cap = 8;
        }
        if (cap < 8) cap = 8;
        unsigned char *bytes = arena_calloc(&g_arena_scratch, cap, 1);
        int byte_len = 0;

        if (d->str_val) {
            // String literal: decoded bytes + NUL
            byte_len = string_bytes(d->str_val, bytes, cap);
            defined = true;
        } else if (d->global_idx >= 0) {
            // Program global
            int gi = d->global_idx;
            if (gi >= 0 && gi < e->prog->num_globals) {
                if (e->prog->globals[gi].is_extern) {
                    // Extern variable: does this file define it?
                    bool defined_here = false;
                    for (int k = 0; k < e->prog->num_globals; k++) {
                        if (k != gi && !e->prog->globals[k].is_extern &&
                            strcmp(e->prog->globals[k].name, e->prog->globals[gi].name) == 0) {
                            defined_here = true;
                            break;
                        }
                    }
                    if (defined_here) {
                        linkage = e->prog->globals[gi].is_public ? "pub" : "local";
                        writable = true;
                        defined = true;
                        if (e->prog->globals[gi].is_string) {
                            byte_len = string_bytes(e->prog->globals[gi].str_val, bytes, cap);
                        } else {
                            byte_len = 8;
                            int64_t v = e->prog->globals[gi].value;
                            for (int k = 0; k < 8; k++) bytes[k] = (v >> (8 * k)) & 0xFF;
                        }
                    } else {
                        int asize = e->prog->globals[gi].array_size * 8;
                        if (asize < 8) asize = 8;
                        fprintf(out, "extern %s r %d\n", n, asize);
                        continue;
                    }
                } else {
                    linkage = e->prog->globals[gi].is_public ? "pub" : "local";
                    writable = true;
                    defined = true;
                    if (e->prog->globals[gi].is_string) {
                        byte_len = string_bytes(e->prog->globals[gi].str_val, bytes, cap);
                    } else if (e->prog->globals[gi].array_size > 0) {
                        int n64 = e->prog->globals[gi].array_size;
                        byte_len = n64 * 8;
                        for (int j = 0; j < n64; j++) {
                            int64_t v = e->prog->globals[gi].array_values
                                            ? e->prog->globals[gi].array_values[j]
                                            : e->prog->globals[gi].value;
                            for (int k = 0; k < 8; k++) {
                                bytes[j * 8 + k] = (v >> (8 * k)) & 0xFF;
                            }
                        }
                    } else {
                        byte_len = 8;
                        int64_t v = e->prog->globals[gi].value;
                        for (int k = 0; k < 8; k++) bytes[k] = (v >> (8 * k)) & 0xFF;
                    }
                }
            }
        }

        if (!defined) {
            // Unknown symbol referenced but never defined: zero data
            byte_len = 8;
            memset(bytes, 0, 8);
        }

        // Pad to 8 bytes so i64 loads stay aligned
        int pad = (8 - (byte_len % 8)) % 8;
        for (int j = 0; j < pad; j++) bytes[byte_len++] = 0;

        fprintf(out, "%s %s %s %d", linkage, n, writable ? "w" : "r", byte_len);
        if (byte_len > 0) {
            fprintf(out, " ");
            for (int j = 0; j < byte_len; j++) {
                if (j > 0) fprintf(out, " ");
                fprintf(out, "%02x", bytes[j]);
            }
        }
        fprintf(out, "\n");
    }
}

// --- top level ---

void emit_cranelift(IRProgram *prog, FILE *out) {
    arena_reset(&g_arena_scratch);

    ClifEmitter e = {0};
    e.out = out;
    e.prog = prog;
    e.num_externs = prog->num_externs;
    e.num_functions = prog->num_functions;
    ir_index_init(&e.index, prog, &g_arena_scratch);

    e.num_externs = prog->num_externs;
    e.implicit_cap = 16;
    e.implicit = arena_calloc(&g_arena_scratch, e.implicit_cap, sizeof(char *));
    e.variadic_cap = 16;
    e.variadic = arena_calloc(&g_arena_scratch, e.variadic_cap, sizeof(char *));

    int max_blocks = 0;
    for (int f = 0; f < prog->num_functions; f++) {
        if (prog->functions[f]->num_blocks > max_blocks)
            max_blocks = prog->functions[f]->num_blocks;
    }
    e.block_order = arena_calloc(&g_arena_scratch, max_blocks ? max_blocks : 1, sizeof(IRBlock *));
    e.block_num = arena_calloc(&g_arena_scratch, max_blocks ? max_blocks : 1, sizeof(int));
    e.slot_cap = 64;
    e.slot_sizes = arena_calloc(&g_arena_scratch, e.slot_cap, sizeof(int));
    e.alloc_vregs = arena_calloc(&g_arena_scratch, e.slot_cap, sizeof(int));
    e.alias = NULL;
    e.num_vregs = 0;

    e.gv_refs_cap = 64;
    e.gv_refs = arena_calloc(&g_arena_scratch, e.gv_refs_cap, sizeof(e.gv_refs[0]));
    e.fn_refs_cap = 64;
    e.fn_refs = arena_calloc(&g_arena_scratch, e.fn_refs_cap, sizeof(e.fn_refs[0]));

    // Pass 1: discover all data references and implicit externs
    DataList dl = {0};
    dl.cap = 64;
    dl.items = arena_calloc(&g_arena_scratch, dl.cap, sizeof(DataRef));
    pre_scan_refs(&e, &dl);
    e.dl = &dl;

    // Pass 2: variadic markers (clifc writes x8-zeroing stubs) then extern
    // function declarations (explicit then implicit)
    for (int i = 0; i < e.num_variadic; i++) {
        fprintf(out, ";; variadic %s\n", e.variadic[i]);
    }
    for (int i = 0; i < prog->num_externs; i++) {
        bool stub = false;
        for (int k = 0; k < e.num_variadic; k++) {
            if (strcmp(e.variadic[k], prog->externs[i].name) == 0) { stub = true; break; }
        }
        emit_extern_decl(out, prog->externs[i].name,
                         prog->externs[i].return_type, 8, stub);
    }
    for (int i = 0; i < e.num_implicit; i++) {
        bool stub = false;
        for (int k = 0; k < e.num_variadic; k++) {
            if (strcmp(e.variadic[k], e.implicit[i]) == 0) { stub = true; break; }
        }
        emit_extern_decl(out, e.implicit[i], IR_TYPE_I64, 8, stub);
    }

    // Pass 3: own functions
    for (int f = 0; f < prog->num_functions; f++) {
        emit_cranelift_function(&e, prog->functions[f]);
    }

    // Pass 4: data
    emit_data_section(&e, &dl);

}
