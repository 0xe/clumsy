#ifndef CFG_H
#define CFG_H

#include "ir.h"
#include <stdio.h>

// Build CFG edges (successors + predecessors) from terminators
void cfg_build(IRFunction *fn);

// Compute dominator tree using iterative algorithm
void cfg_compute_dominators(IRFunction *fn);

// Compute dominance frontiers for all blocks
void cfg_compute_dominance_frontiers(IRFunction *fn);

// Place phi functions at dominance frontiers (requires liveness analysis)
void cfg_place_phis(IRFunction *fn);

// Rename variables to SSA form (dominator-tree DFS)
void cfg_ssa_rename(IRFunction *fn);

// Run SSA pipeline: build → doms → rename (no phis — memory-based IR)
void cfg_ssa_pipeline(IRFunction *fn);

typedef enum {
    CFG_DOT_CFG,   // control flow edges
    CFG_DOT_DOM,   // dominator tree
    CFG_DOT_DF,    // dominance frontiers
} CFGDotMode;

// Dump CFG as Graphviz DOT to the given file
void cfg_dump_dot(IRFunction *fn, FILE *out, CFGDotMode mode);

#endif
