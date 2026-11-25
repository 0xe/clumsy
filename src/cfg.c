#include "cfg.h"
#include "arena.h"
#include <stdlib.h>
#include <string.h>

// ==================== CFG Edge Building ====================

static IRBlock *find_block(IRFunction *fn, const char *label) {
    for (int i = 0; i < fn->num_blocks; i++) {
        if (strcmp(fn->blocks[i]->label, label) == 0)
            return fn->blocks[i];
    }
    return NULL;
}

static void add_succ(IRBlock *src, IRBlock *dst) {
    if (!dst) return;
    for (int i = 0; i < src->num_succs; i++) {
        if (src->succs[i] == dst) return;
    }
    if (src->num_succs >= src->succ_capacity) {
        int old_capacity = src->succ_capacity;
        src->succ_capacity *= 2;
        src->succs = arena_grow(&g_arena_compile, src->succs,
                                (size_t)old_capacity * sizeof(IRBlock *),
                                (size_t)src->succ_capacity * sizeof(IRBlock *));
    }
    src->succs[src->num_succs++] = dst;
}

static void add_pred(IRBlock *dst, IRBlock *src) {
    if (!dst) return;
    for (int i = 0; i < dst->num_preds; i++) {
        if (dst->preds[i] == src) return;
    }
    if (dst->num_preds >= dst->pred_capacity) {
        int old_capacity = dst->pred_capacity;
        dst->pred_capacity *= 2;
        dst->preds = arena_grow(&g_arena_compile, dst->preds,
                                (size_t)old_capacity * sizeof(IRBlock *),
                                (size_t)dst->pred_capacity * sizeof(IRBlock *));
    }
    dst->preds[dst->num_preds++] = src;
}

static void wire_edge(IRBlock *src, IRBlock *dst) {
    add_succ(src, dst);
    add_pred(dst, src);
}

void cfg_build(IRFunction *fn) {
    for (int i = 0; i < fn->num_blocks; i++) {
        fn->blocks[i]->num_succs = 0;
        fn->blocks[i]->num_preds = 0;
    }

    for (int i = 0; i < fn->num_blocks; i++) {
        IRBlock *b = fn->blocks[i];
        if (b->num_instrs == 0) continue;

        IRInstr *last = &b->instrs[b->num_instrs - 1];

        switch (last->op) {
        case IR_JMP: {
            IRBlock *target = find_block(fn, last->args[0].block);
            wire_edge(b, target);
            break;
        }
        case IR_JNZ: {
            IRBlock *then_blk = find_block(fn, last->args[1].block);
            IRBlock *else_blk = find_block(fn, last->args[2].block);
            wire_edge(b, then_blk);
            wire_edge(b, else_blk);
            break;
        }
        case IR_RET:
        default:
            break;
        }
    }
}

// ==================== Dominator Tree ====================

// Compute reverse postorder (RPO) numbering for blocks
static int compute_rpo(IRFunction *fn) {
    // Simple iterative RPO: entry is 0, rest assigned by BFS then reversed
    int n = fn->num_blocks;
    if (n == 0) return 0;

    // Map label -> block index for quick lookup
    int *visited = arena_calloc(&g_arena_scratch, n, sizeof(int));
    int *rpo = arena_calloc(&g_arena_scratch, n, sizeof(int));
    int rpo_count = 0;

    // DFS-based RPO from entry
    // Use iterative stack to avoid recursion
    typedef struct { IRBlock *blk; int phase; } Frame;
    Frame *stack = arena_calloc(&g_arena_scratch, n * 4, sizeof(Frame));
    int sp = 0;

    // Push entry
    stack[sp++] = (Frame){fn->entry, 0};

    while (sp > 0) {
        if (sp >= n * 4) break; // safety bound
        Frame *f = &stack[sp - 1];
        IRBlock *b = f->blk;

        // Find index of b in blocks array
        int idx = -1;
        for (int i = 0; i < n; i++) {
            if (fn->blocks[i] == b) { idx = i; break; }
        }
        if (idx < 0) { sp--; continue; }

        if (f->phase == 0) {
            f->phase = 1;
            if (visited[idx]) {
                sp--;
                continue;
            }
            visited[idx] = 1;

            // Push successors in reverse order for correct RPO
            for (int i = b->num_succs - 1; i >= 0; i--) {
                IRBlock *succ = b->succs[i];
                int sidx = -1;
                for (int j = 0; j < n; j++) {
                    if (fn->blocks[j] == succ) { sidx = j; break; }
                }
                if (sidx >= 0 && !visited[sidx]) {
                    stack[sp++] = (Frame){succ, 0};
                }
            }
        } else {
            // Postorder: add to RPO list
            rpo[rpo_count++] = idx;
            sp--;
        }
    }

    // Assign RPO indices (reverse postorder = reverse of postorder list)
    for (int i = 0; i < n; i++) {
        fn->blocks[i]->rpo_index = -1;
    }
    for (int i = 0; i < rpo_count; i++) {
        fn->blocks[rpo[i]]->rpo_index = rpo_count - 1 - i;
    }
    // Any unreachable blocks get RPO at the end
    for (int i = 0; i < n; i++) {
        if (fn->blocks[i]->rpo_index == -1) {
            fn->blocks[i]->rpo_index = rpo_count++;
        }
    }

    return rpo_count;
}

// Intersect two blocks in dominator tree
static IRBlock *intersect(IRBlock *b1, IRBlock *b2) {
    while (b1 != b2) {
        if (!b1 || !b2) return NULL;
        while (b1 && b2 && b1->rpo_index > b2->rpo_index)
            b1 = b1->idom;
        while (b2 && b1 && b2->rpo_index > b1->rpo_index)
            b2 = b2->idom;
    }
    return b1;
}

void cfg_compute_dominators(IRFunction *fn) {
    if (fn->num_blocks == 0) return;

    compute_rpo(fn);

    // Initialize: entry dominates itself, all others undefined
    for (int i = 0; i < fn->num_blocks; i++) {
        fn->blocks[i]->idom = NULL;
    }
    fn->entry->idom = fn->entry;

    bool changed = true;
    while (changed) {
        changed = false;
        for (int i = 0; i < fn->num_blocks; i++) {
            IRBlock *b = fn->blocks[i];
            if (b == fn->entry) continue;

            IRBlock *new_idom = NULL;
            for (int j = 0; j < b->num_preds; j++) {
                if (b->preds[j]->idom) {
                    new_idom = b->preds[j];
                    break;
                }
            }
            if (!new_idom) continue;

            for (int j = 0; j < b->num_preds; j++) {
                if (b->preds[j]->idom && b->preds[j] != new_idom) {
                    new_idom = intersect(new_idom, b->preds[j]);
                    if (!new_idom) break;
                }
            }

            if (new_idom && b->idom != new_idom) {
                b->idom = new_idom;
                changed = true;
            }
        }
    }

    // Build dominator tree children
    for (int i = 0; i < fn->num_blocks; i++) {
        IRBlock *b = fn->blocks[i];
        b->dom_children = NULL;
        b->num_dom_children = 0;
    }
    for (int i = 0; i < fn->num_blocks; i++) {
        IRBlock *b = fn->blocks[i];
        if (b->idom && b->idom != b) {
            IRBlock *p = b->idom;
            int old_count = p->num_dom_children;
            p->num_dom_children++;
            p->dom_children = arena_grow(&g_arena_compile, p->dom_children,
                                         (size_t)old_count * sizeof(IRBlock *),
                                         (size_t)p->num_dom_children * sizeof(IRBlock *));
            p->dom_children[p->num_dom_children - 1] = b;
        }
    }
}

// ==================== Dominance Frontiers ====================

// Add block to dominance frontier set
static void add_df(IRBlock *b, IRBlock *df) {
    for (int i = 0; i < b->num_dom_frontiers; i++) {
        if (b->dom_frontiers[i] == df) return;
    }
    if (b->num_dom_frontiers >= b->df_capacity) {
        int old_capacity = b->df_capacity;
        b->df_capacity *= 2;
        b->dom_frontiers = arena_grow(&g_arena_compile, b->dom_frontiers,
                                      (size_t)old_capacity * sizeof(IRBlock *),
                                      (size_t)b->df_capacity * sizeof(IRBlock *));
    }
    b->dom_frontiers[b->num_dom_frontiers++] = df;
}

void cfg_compute_dominance_frontiers(IRFunction *fn) {
    // Clear existing frontiers
    for (int i = 0; i < fn->num_blocks; i++) {
        fn->blocks[i]->num_dom_frontiers = 0;
    }

    for (int i = 0; i < fn->num_blocks; i++) {
        IRBlock *b = fn->blocks[i];
        if (b->num_preds < 2) continue;

        // For each predecessor p of b
        for (int j = 0; j < b->num_preds; j++) {
            IRBlock *runner = b->preds[j];
            while (runner && runner != b->idom) {
                add_df(runner, b);
                runner = runner->idom;
            }
        }
    }
}

// ==================== SSA Phi Placement ====================

// Track which variables are defined in each block
typedef struct {
    char **names;      // variable names defined here
    int num_names;
    int capacity;
} DefSet;

// Collect all variable names defined (via STORE) in the function
static void collect_var_names(IRFunction *fn, char ***out_names, int *out_count) {
    int capacity = 64;
    char **names = arena_calloc(&g_arena_scratch, capacity, sizeof(char *));
    int count = 0;

    for (int i = 0; i < fn->num_blocks; i++) {
        IRBlock *b = fn->blocks[i];
        for (int j = 0; j < b->num_instrs; j++) {
            IRInstr *instr = &b->instrs[j];
            // Variables are destinations of LOAD or STORE targets
            if (instr->dest.type == OP_REGISTER) {
                // Check if this name is already in the list
                bool found = false;
                for (int k = 0; k < count; k++) {
                    if (strcmp(names[k], instr->dest.reg) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    if (count >= capacity) {
                        capacity *= 2;
                        int old_capacity = capacity / 2;
                        names = arena_grow(&g_arena_scratch, names,
                                           (size_t)old_capacity * sizeof(char *),
                                           (size_t)capacity * sizeof(char *));
                    }
                    names[count++] = arena_strdup(&g_arena_scratch, instr->dest.reg);
                }
            }
            // Also check STORE targets (second arg) and LOAD source (first arg)
            for (int a = 0; a < 3; a++) {
                if (instr->args[a].type == OP_REGISTER) {
                    bool found = false;
                    for (int k = 0; k < count; k++) {
                        if (strcmp(names[k], instr->args[a].reg) == 0) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        if (count >= capacity) {
                            capacity *= 2;
                            int old_capacity = capacity / 2;
                        names = arena_grow(&g_arena_scratch, names,
                                           (size_t)old_capacity * sizeof(char *),
                                           (size_t)capacity * sizeof(char *));
                        }
                        names[count++] = arena_strdup(&g_arena_scratch, instr->args[a].reg);
                    }
                }
            }
        }
    }

    *out_names = names;
    *out_count = count;
}

// Check if a variable is defined in a block
static bool is_defined_in(IRBlock *b, const char *name) {
    for (int i = 0; i < b->num_instrs; i++) {
        IRInstr *instr = &b->instrs[i];
        if (instr->dest.type == OP_REGISTER && strcmp(instr->dest.reg, name) == 0)
            return true;
    }
    return false;
}

void cfg_place_phis(IRFunction *fn) {
    char **var_names = NULL;
    int num_vars = 0;
    collect_var_names(fn, &var_names, &num_vars);

    for (int v = 0; v < num_vars; v++) {
        const char *name = var_names[v];

        int *worklist = arena_calloc(&g_arena_scratch, fn->num_blocks, sizeof(int));
        int wl_count = 0;
        bool *in_worklist = arena_calloc(&g_arena_scratch, fn->num_blocks, sizeof(bool));
        bool *has_phi = arena_calloc(&g_arena_scratch, fn->num_blocks, sizeof(bool));

        // Seed: blocks that define this variable
        for (int i = 0; i < fn->num_blocks; i++) {
            if (is_defined_in(fn->blocks[i], name)) {
                worklist[wl_count++] = i;
                in_worklist[i] = true;
            }
        }

        // Iterate dominance frontiers
        int processed = 0;
        while (processed < wl_count) {
            int idx = worklist[processed++];
            IRBlock *b = fn->blocks[idx];

            for (int df = 0; df < b->num_dom_frontiers; df++) {
                IRBlock *df_block = b->dom_frontiers[df];
                int df_idx = -1;
                for (int i = 0; i < fn->num_blocks; i++) {
                    if (fn->blocks[i] == df_block) { df_idx = i; break; }
                }

                if (df_idx >= 0 && !has_phi[df_idx]) {
                    has_phi[df_idx] = true;

                    IRInstr phi = {0};
                    phi.op = IR_PHI;
                    phi.dest = op_reg(name);
                    phi.dest_type = IR_TYPE_I64;
                    phi.phi_count = df_block->num_preds;

                    for (int p = 0; p < df_block->num_preds && p < 8; p++) {
                        phi.phi_pairs[p * 2] = op_reg(name);
                        phi.phi_pairs[p * 2 + 1].type = OP_BLOCK;
                        phi.phi_pairs[p * 2 + 1].block = ir_strdup(&g_arena_compile, df_block->preds[p]->label);
                    }

                    // Ensure capacity
                    if (df_block->num_instrs >= df_block->capacity) {
                        int old_capacity = df_block->capacity;
                        df_block->capacity *= 2;
                        df_block->instrs = arena_grow(&g_arena_compile, df_block->instrs,
                                                      (size_t)old_capacity * sizeof(IRInstr),
                                                      (size_t)df_block->capacity * sizeof(IRInstr));
                    }
                    // Shift existing instrs right by 1
                    for (int k = df_block->num_instrs; k > 0; k--) {
                        df_block->instrs[k] = df_block->instrs[k - 1];
                    }
                    df_block->instrs[0] = phi;
                    df_block->num_instrs++;

                    if (!in_worklist[df_idx]) {
                        worklist[wl_count++] = df_idx;
                        in_worklist[df_idx] = true;
                    }
                }
            }
        }

    }
}

// ==================== SSA Renaming ====================

// Per-variable version stack for renaming
typedef struct {
    char *name;
    int *versions;
    int top;
    int capacity;
} VarStack;

static void var_stack_push(VarStack *s, int version) {
    if (s->top >= s->capacity) {
        int old_capacity = s->capacity;
        s->capacity *= 2;
        s->versions = arena_grow(&g_arena_scratch, s->versions,
                                 (size_t)old_capacity * sizeof(int),
                                 (size_t)s->capacity * sizeof(int));
    }
    s->versions[s->top++] = version;
}

static int var_stack_top(VarStack *s) {
    return s->top > 0 ? s->versions[s->top - 1] : -1;
}

// Replace register operand with current SSA version
static void rename_operand(IROperand *op, VarStack *stacks, int num_stacks) {
    if (op->type != OP_REGISTER) return;

    for (int i = 0; i < num_stacks; i++) {
        if (strcmp(op->reg, stacks[i].name) == 0) {
            int v = var_stack_top(&stacks[i]);
            if (v >= 0) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%%v_%d", v);
                op->reg = ir_strdup(&g_arena_compile, buf);
                op->type = OP_VREG;
            }
            return;
        }
    }
}

// Replace dest with new SSA version, push onto stack
static void rename_dest(IROperand *op, VarStack *stacks, int num_stacks, IRFunction *fn) {
    if (op->type != OP_REGISTER) return;

    for (int i = 0; i < num_stacks; i++) {
        if (strcmp(op->reg, stacks[i].name) == 0) {
            int new_v = fn->vreg_counter++;
            var_stack_push(&stacks[i], new_v);
            char buf[32];
            snprintf(buf, sizeof(buf), "%%v_%d", new_v);
            op->reg = ir_strdup(&g_arena_compile, buf);
            op->type = OP_VREG;
            return;
        }
    }
}

// Rename all instructions in a block (except phi dests — those are done separately)
static void rename_block_instructions(IRBlock *b, VarStack *stacks, int num_stacks, IRFunction *fn) {
    for (int i = 0; i < b->num_instrs; i++) {
        IRInstr *instr = &b->instrs[i];

        // Phi dests are renamed at block entry, skip them here
        if (instr->op == IR_PHI) continue;

        // Rename dest (non-phi)
        if (instr->dest.type == OP_REGISTER) {
            rename_dest(&instr->dest, stacks, num_stacks, fn);
        }

        // Rename args
        for (int a = 0; a < 8; a++) {
            rename_operand(&instr->args[a], stacks, num_stacks);
        }
    }
}

// Rename phi dests in a block
static void rename_phi_dests(IRBlock *b, VarStack *stacks, int num_stacks, IRFunction *fn) {
    for (int i = 0; i < b->num_instrs; i++) {
        IRInstr *instr = &b->instrs[i];
        if (instr->op != IR_PHI) break; // phis are always first
        rename_dest(&instr->dest, stacks, num_stacks, fn);
    }
}

// Rename phi args for a specific successor block (based on which predecessor we came from)
static void rename_phi_args_for_pred(IRBlock *succ, IRBlock *pred,
                                     VarStack *stacks, int num_stacks) {
    for (int i = 0; i < succ->num_instrs; i++) {
        IRInstr *instr = &succ->instrs[i];
        if (instr->op != IR_PHI) break;

        // Find the phi pair that corresponds to this predecessor
        for (int p = 0; p < instr->phi_count; p++) {
            if (instr->phi_pairs[p * 2 + 1].type == OP_BLOCK &&
                strcmp(instr->phi_pairs[p * 2 + 1].block, pred->label) == 0) {
                rename_operand(&instr->phi_pairs[p * 2], stacks, num_stacks);
            }
        }
    }
}

// Dominator-tree DFS rename
static bool *visited_blocks;

static void rename_dfs(IRBlock *b, VarStack *stacks, int num_stacks, IRFunction *fn) {
    int b_idx = -1;
    for (int i = 0; i < fn->num_blocks; i++) {
        if (fn->blocks[i] == b) { b_idx = i; break; }
    }
    if (b_idx < 0 || visited_blocks[b_idx]) return;
    visited_blocks[b_idx] = true;

    // Save stack depths for backtracking
    int *saved_tops = arena_calloc(&g_arena_scratch, num_stacks, sizeof(int));
    for (int i = 0; i < num_stacks; i++) {
        saved_tops[i] = stacks[i].top;
    }

    // Rename phi dests at block entry
    rename_phi_dests(b, stacks, num_stacks, fn);

    // Rename all other instructions in this block
    rename_block_instructions(b, stacks, num_stacks, fn);

    // For each successor, rename phi args pointing back to this block
    for (int s = 0; s < b->num_succs; s++) {
        rename_phi_args_for_pred(b->succs[s], b, stacks, num_stacks);
    }

    // Recurse into dominator children
    for (int c = 0; c < b->num_dom_children; c++) {
        rename_dfs(b->dom_children[c], stacks, num_stacks, fn);
    }

    // Restore stack depths (backtrack)
    for (int i = 0; i < num_stacks; i++) {
        stacks[i].top = saved_tops[i];
    }
}

void cfg_ssa_rename(IRFunction *fn) {
    char **var_names = NULL;
    int num_vars = 0;
    collect_var_names(fn, &var_names, &num_vars);

    if (num_vars == 0) {
        return;
    }

    // Initialize version stacks with -1 (undefined)
    VarStack *stacks = arena_calloc(&g_arena_scratch, num_vars, sizeof(VarStack));
    for (int i = 0; i < num_vars; i++) {
        stacks[i].name = var_names[i];
        stacks[i].capacity = 8;
        stacks[i].versions = arena_calloc(&g_arena_scratch, stacks[i].capacity, sizeof(int));
        stacks[i].top = 0;
        var_stack_push(&stacks[i], -1);
    }

    visited_blocks = arena_calloc(&g_arena_scratch, fn->num_blocks, sizeof(bool));

    // DFS rename from entry along dominator tree
    rename_dfs(fn->entry, stacks, num_vars, fn);

}

// ==================== SSA Pipeline ====================

void cfg_ssa_pipeline(IRFunction *fn) {
    arena_reset(&g_arena_scratch);
    cfg_build(fn);
    cfg_compute_dominators(fn);
    cfg_ssa_rename(fn);
}

// ==================== DOT Dump ====================

void cfg_dump_dot(IRFunction *fn, FILE *out, CFGDotMode mode) {
    fprintf(out, "digraph %s {\n", fn->name);
    fprintf(out, "  node [shape=box, fontname=\"monospace\"];\n");
    fprintf(out, "  rankdir=TB;\n\n");

    if (mode == CFG_DOT_CFG) {
        // Control flow edges
        for (int i = 0; i < fn->num_blocks; i++) {
            IRBlock *b = fn->blocks[i];
            const char *label = b->label;
            if (label[0] == '@') label++;

            bool is_entry = (b == fn->entry);
            bool is_exit = (b->num_succs == 0);

            fprintf(out, "  \"%s\" [", label);
            if (is_entry || is_exit) {
                fprintf(out, "style=filled, fillcolor=");
                if (is_entry && is_exit)
                    fprintf(out, "\"#FFD700\"");
                else if (is_entry)
                    fprintf(out, "\"#90EE90\"");
                else
                    fprintf(out, "\"#FFB6C1\"");
            }
            fprintf(out, "label=\"%s\"];\n", b->label);
        }

        fprintf(out, "\n");

        for (int i = 0; i < fn->num_blocks; i++) {
            IRBlock *b = fn->blocks[i];
            const char *src_label = b->label;
            if (src_label[0] == '@') src_label++;

            IRInstr *last = (b->num_instrs > 0) ? &b->instrs[b->num_instrs - 1] : NULL;

            for (int j = 0; j < b->num_succs; j++) {
                const char *dst_label = b->succs[j]->label;
                if (dst_label[0] == '@') dst_label++;

                fprintf(out, "  \"%s\" -> \"%s\"", src_label, dst_label);

                if (last && last->op == IR_JMP && b->num_succs == 1) {
                    fprintf(out, " [style=dashed, color=gray]");
                }

                fprintf(out, ";\n");
            }
        }
    } else if (mode == CFG_DOT_DOM) {
        // Dominator tree edges
        for (int i = 0; i < fn->num_blocks; i++) {
            IRBlock *b = fn->blocks[i];
            const char *label = b->label;
            if (label[0] == '@') label++;

            bool is_entry = (b == fn->entry);

            fprintf(out, "  \"%s\" [", label);
            if (is_entry)
                fprintf(out, "style=filled, fillcolor=\"#90EE90\"");
            fprintf(out, "label=\"%s\"];\n", b->label);
        }

        fprintf(out, "\n");

        for (int i = 0; i < fn->num_blocks; i++) {
            IRBlock *b = fn->blocks[i];
            if (b->idom && b->idom != b) {
                const char *src_label = b->idom->label;
                const char *dst_label = b->label;
                if (src_label[0] == '@') src_label++;
                if (dst_label[0] == '@') dst_label++;

                fprintf(out, "  \"%s\" -> \"%s\" [color=blue];\n", src_label, dst_label);
            }
        }
    } else if (mode == CFG_DOT_DF) {
        // Dominance frontiers as dotted edges
        for (int i = 0; i < fn->num_blocks; i++) {
            IRBlock *b = fn->blocks[i];
            const char *label = b->label;
            if (label[0] == '@') label++;

            bool is_entry = (b == fn->entry);
            bool is_exit = (b->num_succs == 0);

            fprintf(out, "  \"%s\" [", label);
            if (is_entry || is_exit) {
                fprintf(out, "style=filled, fillcolor=");
                if (is_entry && is_exit)
                    fprintf(out, "\"#FFD700\"");
                else if (is_entry)
                    fprintf(out, "\"#90EE90\"");
                else
                    fprintf(out, "\"#FFB6C1\"");
            }
            fprintf(out, "label=\"%s\"];\n", b->label);
        }

        fprintf(out, "\n");

        // CFG edges in light gray
        for (int i = 0; i < fn->num_blocks; i++) {
            IRBlock *b = fn->blocks[i];
            const char *src_label = b->label;
            if (src_label[0] == '@') src_label++;

            for (int j = 0; j < b->num_succs; j++) {
                const char *dst_label = b->succs[j]->label;
                if (dst_label[0] == '@') dst_label++;
                fprintf(out, "  \"%s\" -> \"%s\" [color=lightgray];\n", src_label, dst_label);
            }
        }

        fprintf(out, "\n");

        // Dominance frontier edges in red
        for (int i = 0; i < fn->num_blocks; i++) {
            IRBlock *b = fn->blocks[i];
            const char *src_label = b->label;
            if (src_label[0] == '@') src_label++;

            for (int df = 0; df < b->num_dom_frontiers; df++) {
                const char *dst_label = b->dom_frontiers[df]->label;
                if (dst_label[0] == '@') dst_label++;
                fprintf(out, "  \"%s\" -> \"%s\" [color=red, style=dashed];\n", src_label, dst_label);
            }
        }
    }

    fprintf(out, "}\n");
}
