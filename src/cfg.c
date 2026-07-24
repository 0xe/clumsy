#include "cfg.h"
#include <stdlib.h>
#include <string.h>

// Find block by label in a function
static IRBlock *find_block(IRFunction *fn, const char *label) {
    for (int i = 0; i < fn->num_blocks; i++) {
        if (strcmp(fn->blocks[i]->label, label) == 0)
            return fn->blocks[i];
    }
    return NULL;
}

// Add a successor edge: src -> dst
static void add_succ(IRBlock *src, IRBlock *dst) {
    if (!dst) return;
    // Avoid duplicate edges
    for (int i = 0; i < src->num_succs; i++) {
        if (src->succs[i] == dst) return;
    }
    if (src->num_succs >= src->succ_capacity) {
        src->succ_capacity *= 2;
        src->succs = realloc(src->succs, src->succ_capacity * sizeof(IRBlock *));
    }
    src->succs[src->num_succs++] = dst;
}

// Add a predecessor edge: dst has predecessor src
static void add_pred(IRBlock *dst, IRBlock *src) {
    if (!dst) return;
    // Avoid duplicate edges
    for (int i = 0; i < dst->num_preds; i++) {
        if (dst->preds[i] == src) return;
    }
    if (dst->num_preds >= dst->pred_capacity) {
        dst->pred_capacity *= 2;
        dst->preds = realloc(dst->preds, dst->pred_capacity * sizeof(IRBlock *));
    }
    dst->preds[dst->num_preds++] = src;
}

// Wire an edge: src -> dst (adds both succ and pred)
static void wire_edge(IRBlock *src, IRBlock *dst) {
    add_succ(src, dst);
    add_pred(dst, src);
}

void cfg_build(IRFunction *fn) {
    // Clear any existing edges
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
            // No successors
            break;
        }
    }
}

void cfg_dump_dot(IRFunction *fn, FILE *out) {
    fprintf(out, "digraph %s {\n", fn->name);
    fprintf(out, "  node [shape=box, fontname=\"monospace\"];\n");
    fprintf(out, "  rankdir=TB;\n\n");

    // Nodes
    for (int i = 0; i < fn->num_blocks; i++) {
        IRBlock *b = fn->blocks[i];
        const char *label = b->label;
        if (label[0] == '@') label++;

        // Entry block is green, exit blocks (no successors) are red
        bool is_entry = (b == fn->entry);
        bool is_exit = (b->num_succs == 0);

        fprintf(out, "  \"%s\" [", label);
        if (is_entry || is_exit) {
            fprintf(out, "style=filled, fillcolor=");
            if (is_entry && is_exit)
                fprintf(out, "\"#FFD700\"");  // gold for entry+exit
            else if (is_entry)
                fprintf(out, "\"#90EE90\"");  // light green
            else
                fprintf(out, "\"#FFB6C1\"");  // light red
        }
        fprintf(out, "label=\"");
        // Build a compact label with first few instructions
        fprintf(out, "%s\\n", b->label);
        int shown = 0;
        for (int j = 0; j < b->num_instrs && shown < 4; j++) {
            IRInstr *instr = &b->instrs[j];
            switch (instr->op) {
            case IR_ADD:   fprintf(out, "+ "); shown++; break;
            case IR_SUB:   fprintf(out, "- "); shown++; break;
            case IR_MUL:   fprintf(out, "* "); shown++; break;
            case IR_DIV:   fprintf(out, "/ "); shown++; break;
            case IR_MOD:   fprintf(out, "%% "); shown++; break;
            case IR_EQ:    fprintf(out, "== "); shown++; break;
            case IR_NE:    fprintf(out, "!= "); shown++; break;
            case IR_LT:    fprintf(out, "< "); shown++; break;
            case IR_GT:    fprintf(out, "> "); shown++; break;
            case IR_LE:    fprintf(out, "<= "); shown++; break;
            case IR_GE:    fprintf(out, ">= "); shown++; break;
            case IR_LOAD:  fprintf(out, "load "); shown++; break;
            case IR_STORE: fprintf(out, "store "); shown++; break;
            case IR_CALL:  fprintf(out, "call "); shown++; break;
            case IR_RET:   fprintf(out, "ret "); shown++; break;
            case IR_MOV:   fprintf(out, "mov "); shown++; break;
            case IR_PHI:   fprintf(out, "phi "); shown++; break;
            case IR_JMP:   fprintf(out, "jmp "); shown++; break;
            case IR_JNZ:   fprintf(out, "jnz "); shown++; break;
            default:       fprintf(out, "..."); shown++; break;
            }
        }
        fprintf(out, "\"];\n");
    }

    fprintf(out, "\n");

    // Edges
    for (int i = 0; i < fn->num_blocks; i++) {
        IRBlock *b = fn->blocks[i];
        const char *src_label = b->label;
        if (src_label[0] == '@') src_label++;

        IRInstr *last = (b->num_instrs > 0) ? &b->instrs[b->num_instrs - 1] : NULL;

        for (int j = 0; j < b->num_succs; j++) {
            const char *dst_label = b->succs[j]->label;
            if (dst_label[0] == '@') dst_label++;

            fprintf(out, "  \"%s\" -> \"%s\"", src_label, dst_label);

            // Style back edges differently (JMP at end of while body → while cond)
            if (last && last->op == IR_JMP && b->num_succs == 1) {
                fprintf(out, " [style=dashed, color=gray]");
            }

            fprintf(out, ";\n");
        }
    }

    fprintf(out, "}\n");
}
