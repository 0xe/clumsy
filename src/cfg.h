#ifndef CFG_H
#define CFG_H

#include "ir.h"
#include <stdio.h>

// Build CFG edges (successors + predecessors) from terminators
void cfg_build(IRFunction *fn);

// Dump CFG as Graphviz DOT to the given file
void cfg_dump_dot(IRFunction *fn, FILE *out);

#endif
