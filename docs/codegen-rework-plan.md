# Codegen Rewrite Plan

## Goal

Replace the monolithic `compiler.c` (AST → ARM64) with a proper IR pipeline: **AST → IR → CFG → SSA → scalar opts → backend**. The goal is to learn advanced compiler construction (SSA, data flow analysis, optimization) while building a production-quality backend system.

**Success criteria**: tetris compiles and runs through the new pipeline.

---

## Architecture

```
                          ┌──────────────────────┐
                          │    Clumsy Source      │
                          └──────────┬───────────┘
                                     ↓
                          ┌──────────────────────┐
                          │  Tokenizer → Parser   │
                          └──────────┬───────────┘
                                     ↓
                          ┌──────────────────────┐
                          │        AST            │
                          └──────────┬───────────┘
                                     ↓
                ┌────────────────────┴────────────────────┐
                ↓                                         ↓
 ┌─────────────────────────┐       ┌──────────────────────────────┐
 │  Legacy Aarch64 Backend │       │  New IR Pipeline             │
 │  macOS only, direct     │       │                              │
 │  AST → ARM64 assembly   │       │  AST → IR → CFG → SSA → Opt │
 │                         │       │              ↓                │
 │  --backend=aarch64-     │       │  Backend Emitter             │
 │    legacy               │       │    ↓       ↓        ↓        │
 └─────────────────────────┘       │   QBE    LLVM   Cranelift    │
                                   └──────────────────────────────┘
```

The legacy backend is invoked via `--backend=aarch64-legacy` (default for now). The new pipeline is invoked via `--backend=qbe` (or later `--backend=llvm`, `--backend=cranelift`). Both backends coexist during development.

---

## Phase 0: Pre-work — Missing Language Features (COMPLETE)

Implemented `||`, `&&`, `!`, `break`, `continue`. All committed (`e27bfe8`).

---

## Phase 1: AST → IR → QBE (Full Language Coverage)

Build the complete IR pipeline for all language features in one pass. No Phase 1/Phase 2 split — the IR data structures are the same regardless; we just grow the translation code.

### Verification Checkpoints

Build incrementally with verification at each step:

1. **IR structures + QBE emitter** — define all types, emit basic QBE IL text
2. **Core translation** — int/arithmetic/comparisons/if/while/ret/functions/locals
3. **Verify core** — simple test program through QBE, compare output with legacy backend
4. **Extend translation** — arrays, structs, pointers, globals, strings, C interop, variadic calls
5. **Verify full** — all 40 tests + tetris through QBE, compare output with legacy backend

### Backend Selection

Add `--backend` flag to `src/main.c`:

```
./clumsyc --backend=aarch64-legacy source.cpl    # Default. Old direct ARM64 (macOS only)
./clumsyc --backend=qbe source.cpl               # IR → QBE IL → assemble
```

Default: `aarch64-legacy`. Both backends coexist during development.

### IR Design

Three-address code. Maps directly to QBE's IL format.

**Types:**
- `i64` — 64-bit integer (all Clumsy values; no double/float yet)
- `ptr` — pointer (8 bytes on stack)
- `void` — no return value

**Instructions:**

```c
typedef enum {
    // Arithmetic (produce i64)
    IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
    IR_AND, IR_OR, IR_XOR, IR_SHL, IR_SHR, IR_NOT,
    IR_NEG,   // arithmetic negation (-x)

    // Comparisons (produce 0 or 1)
    IR_EQ, IR_NE, IR_LT, IR_GT, IR_LE, IR_GE,

    // Memory
    IR_LOAD,     // %x = load ptr, offset
    IR_STORE,    // store val, ptr, offset
    IR_ALLOC,    // %x = alloc size bytes on stack

    // Control
    IR_JMP,      // jmp @label
    IR_JNZ,      // jnz cond, @then, @else
    IR_RET,      // ret val (or ret void)

    // Calls
    IR_CALL,     // %x = call $func(args...)
                 // call $printf(l $fmt, ..., l %arg)  // variadic

    // SSA
    IR_PHI,      // %x = phi [(%v1, @b1), (%v2, @b2)]

    // Constants
    IR_MOV,      // %x = mov val (immediate or register)

    // Address
    IR_ADDR,     // %x = addr $label (address of data/string/function)
} IROpcode;
```

**Operand types:**
```c
typedef enum {
    OP_NONE,
    OP_IMM,      // immediate integer constant
    OP_REGISTER, // virtual register (%name)
    OP_BLOCK,    // basic block label (@label)
    OP_GLOBAL,   // global symbol ($name)
} IROperandType;

typedef struct {
    IROperandType type;
    union {
        int64_t imm;
        char *reg;       // virtual register name
        char *block;     // basic block label
        char *global;    // global symbol (function, string data)
    };
} IROperand;

typedef struct {
    IROpcode op;
    IROperand dest;        // result register (or OP_NONE)
    IROperand args[3];     // up to 3 source operands
    IROperand phi_pairs[8]; // for IR_PHI: [val, block, val, block, ...]
    int phi_count;
    int size;              // w (32-bit) or l (64-bit) for QBE
} IRInstr;
```

**Basic blocks and functions:**
```c
typedef struct IRBlock {
    char *label;
    IRInstr *instrs;
    int num_instrs;
    int capacity;
    struct IRBlock **succs;   // successor blocks (CFG edges)
    int num_succs;

    // Dominator tree (computed later in Phase 3)
    struct IRBlock *idom;     // immediate dominator
    struct IRBlock **dom_children;
    int num_dom_children;

    // SSA (computed later in Phase 4)
    // Phi functions live in instrs[]
} IRBlock;

typedef struct {
    char *name;
    IRBlock *entry;
    IRBlock **blocks;
    int num_blocks;
    int num_params;

    // String literals (emitted as data directives)
    struct { char *label; char *value; } *strings;
    int num_strings;

    // Global data (for pub/let globals)
    struct { char *name; int64_t value; bool is_string; char *str_val; int array_size; } *globals;
    int num_globals;
} IRFunction;
```

### Files

| File | Purpose |
|------|---------|
| `src/ir.h` | IR data structures |
| `src/ir.c` | IR construction helpers (`ir_new_instr`, `ir_new_block`, `ir_append_instr`) |
| `src/ir_build.c` | AST → IR translation (`ir_build_function`, `ir_build_expr`, `ir_build_stmt`) |
| `src/emit_qbe.c` | IR → QBE IL text emission (`emit_qbe_function`, `emit_qbe_program`) |

### What to translate (AST node → IR)

| AST Node | IR |
|----------|----|
| `AST_INT` | `mov %t, imm` |
| `AST_STRING` | `addr $str_N` (string added to data section) |
| `AST_IDENTIFIER` | `mov %t, %var` (register move from variable) |
| Binary arithmetic `(+ a b)` | `add %t, %a, %b` |
| Binary comparison `(= a b)` | `ceq %t, %a, %b` |
| `(&& a b)` | Short-circuit: jnz sequence |
| `(not x)` / `(! x)` | `ceq %t, %x, 0` |
| `(let x type val)` | `alloc 16` for variable + `store %val, %ptr` |
| `(set x val)` | `store %val, %x_ptr` |
| `(if c t f)` | Blocks: entry → cond → then → merge; cond jnz to then/else |
| `(while c body)` | Blocks: cond → body → cond; jnz in cond to body/exit |
| `(ret val)` | `ret %val` |
| `(fn params ret body)` | Full function IR with prologue/epilogue |
| `(call f args)` | `call $f(%a, %b, ...)` |
| `(extern ...)` | No IR — external declarations are handled at emit time |
| `arr[i]` (load) | `load %arr + i*8` (8 bytes per element) |
| `(set arr[i] val)` (store) | `store %val, %arr + i*8` |
| `(& x)` (address-of) | `mov %t, %x` (variable is already a pointer) |
| `(* ptr)` (dereference) | `load %t, %ptr` |
| `(<< a b)` | `shl %t, %a, %b` |
| `(>> a b)` | `shr %t, %a, %b` |
| `(& a b)` (bitwise AND) | `and %t, %a, %b` |
| `(~ x)` (bitwise NOT) | `not %t, %x` |
| `(** a b)` (exponentiation) | `call $__clumsy_pow(%a, %b)` — helper function |
| `println` | Normalized to `call $printf(...)` before IR |

### Design Decisions

- **Exponentiation**: `(** a b)` remains an `IR_CALL` to helper function `__clumsy_pow`
- **Print**: `println` normalized to `printf` call before IR generation — no special IR instruction
- **Variadic calls**: QBE IL syntax `call $printf(l $fmt, ..., l %arg)` handled in emit_qbe.c
- **String literals**: Emitted as `data $str_N = { b "text", b 0 }` in QBE
- **Global variables**: Emitted as `data $name = { l value }` in QBE

### Verification

```bash
# Step 1: IR text output for inspection
./clumsyc --backend=qbe examples/simple_arith.cpl > /tmp/test.ir
cat /tmp/test.ir   # manual inspection

# Step 2: Full pipeline — IR → QBE → assemble → run
./clumsyc --backend=qbe examples/simple_arith.cpl > /tmp/test.ssa
qbe -o /tmp/test.s /tmp/test.ssa
cc -o /tmp/test /tmp/test.s
/tmp/test

# Step 3: Compare output with legacy backend
./build/clumsyc examples/simple_arith.cpl | ./build/clumsyc --backend=qbe examples/simple_arith.cpl -
# (or run both and compare stdout)
```

### End-to-end test

Start with simple arithmetic programs, verify through QBE pipeline. Then extend to all 40 tests + tetris.

---

## Phase 2: CFG Construction + Dominator Analysis (COMPLETE)

This is where the learning begins (Weeks 2-4 of the reading guide).

### CFG Construction

```c
// After IR building, link blocks via successor/predecessor edges
void cfg_build(IRFunction *fn);

// Each block's terminators define its successors:
//   IR_JMP: 1 successor
//   IR_JNZ: 2 successors (then, else)
//   IR_RET: 0 successors
```

### Dominator Tree

```c
// Iterative dominator algorithm (simpler than Lengauer-Tarjan for learning)
void compute_dominators(IRFunction *fn);

// For each block B:
//   idom(B) = the block that dominates B and is dominated by all other dominators of B
```

### Dominance Frontiers

```c
// DF(B) = set of blocks X where B dominates a predecessor of X
//         but B does not strictly dominate X
// Needed for phi insertion in Phase 3
void compute_dominance_frontiers(IRFunction *fn);
```

### Verification

Print dominator tree and dominance frontiers. Compare with hand-computed results for small test programs.

---

## Phase 3: SSA Construction (COMPLETE — see note below)

Core learning exercise (Weeks 5-8 of the reading guide). Implements the Braun et al. 2013 algorithm.

### Step 1: Phi Function Insertion

```
for each variable v:
    for each block B in DF(v.decl_block):
        insert phi: v = phi(v, v, ...) with one arg per predecessor
        add B to v.def_blocks
        if B is not in DF(v.decl_block) originally:
            recompute DF
```

### Step 2: Variable Renaming

```
// Recursive DFS from entry block
rename(block):
    for each instruction in block:
        for each operand op in instruction:
            if op is a variable:
                replace op with current_version(op)
        if instruction defines a variable v:
            v.version_counter++
            current_version(v) = v.version_counter
    for each successor block:
        update phi arguments in successor
    for each dominator tree child:
        rename(child)
    // Backtrack: restore current_version for variables defined in this block
```

### SSA IR Representation

In the IR, SSA variables are versioned:
```
x_0 = alloc 8          ; initial version
store 42, x_0          ; first definition
... = load x_0          ; use
x_1 = phi [(x_0, @b1), (x_2, @b2)]  ; phi creates new version
... = load x_1          ; use phi result
```

### Verification

Print SSA IR for test programs. Verify:
- Every variable is defined exactly once
- Every use reaches its definition (dominance property)
- Phi functions are at correct locations (dominance frontiers)

### Implementation status

Built in `src/cfg.c` as the **dominator-tree DFS rename** (`cfg_ssa_rename`)
instead of Braun et al.: compute dominators (`cfg_compute_dominators`),
dominance frontiers (`cfg_compute_dominance_frontiers`), then rename along the
dominator tree. `cfg_place_phis` exists but is **not in the pipeline** — the
memory-based IR (every variable is an `alloca` slot) means alloc pointers serve
as merge points, so phis aren't needed for correctness; phi placement would
require liveness analysis. Wired in via `cfg_ssa_pipeline` in `main.c` before
emission; `--dump-ssa` prints SSA IR + dominator tree + frontiers.

---

## Phase 4: Scalar Optimizations

Each optimization is an IR→IR pass (Weeks 9-10 of the reading guide).

### Dead Code Elimination (DCE)

```
// Mark all live instructions (those whose result is used or is a side effect)
// Remove unmarked instructions
// Repeat until fixed point
```

### Constant Folding

```
// If both operands of an arithmetic/comparison instruction are immediates:
//   evaluate at compile time and replace with mov
// Example: add 3, 4 → mov 7
```

### Constant Propagation

```
// If a mov instruction assigns an immediate to a register:
//   substitute all subsequent uses of that register with the immediate
//   (until the register is redefined)
```

### Pass Manager

```c
typedef void (*IRPass)(IRFunction *fn);

void ir_optimize(IRFunction *fn) {
    ir_pass_constant_propagation(fn);
    ir_pass_constant_folding(fn);
    ir_pass_dce(fn);
}
```

---

## Phase 5: Future Backends (not in initial scope)

After Phase 3 is solid, add backends incrementally:

### LLVM IR Emission — IN PROGRESS

Detailed plan in `docs/llvm-backend.md`. Mirrors `emit_qbe.c` with a new
`src/emit_llvm.c`, `--backend=llvm`, and an `llc`-based test pipeline. LLVM 18
opaque-pointer conventions; all IR values map to `i64`, boundary conversion via
`inttoptr`/`ptrtoint`.

### Cranelift

Rust-based backend. Uses its own IL (Cranelift IR). Could be invoked via CLI like QBE, or embedded as a library. Worth investigating for its simpler design compared to LLVM.

### Native ARM64 Backend

Register allocation, instruction selection, ABI compliance. The most complex option but gives full control. Would implement:
- Linear scan register allocation
- ARM64 instruction selection
- Stack frame layout
- macOS/Linux calling conventions
- String literal emission

---

## Backend Selection

```
./clumsyc --backend=aarch64-legacy source.cpl    # Old direct ARM64 (macOS only)
./clumsyc --backend=qbe source.cpl               # IR → QBE IL → assemble  (DEFAULT)
./clumsyc --backend=llvm source.cpl              # IR → LLVM IR → assemble (in progress)
./clumsyc --backend=qbe --opt source.cpl         # With scalar optimizations (future)
./clumsyc --backend=cranelift source.cpl          # (future) IR → CLIF → assemble
```

Default: `qbe`.

---

## Testing Strategy

- **Regression**: All 40 existing tests pass with both `--backend=aarch64-legacy` and `--backend=qbe`
- **Output comparison**: For each test, stdout of both backends must be identical
- **IR-level tests**: New tests in `tests/ir/` that verify IR output, SSA form, optimization results
- **Tetris**: Compiles and runs through both backends
- **Examples**: All examples compile and run through both backends

Verification checkpoints:
- Phase 1 checkpoint 1: Simple programs compile through QBE pipeline
- Phase 1 checkpoint 2: All programs compile through QBE pipeline
- Phase 2: CFG and dominator tree print correctly
- Phase 3: SSA form is correct (every variable defined once, uses reach definitions)
- Phase 4: Optimizations produce correct results (compare output before/after)

---

## File Changes Summary

### New files
| File | Phase | Purpose |
|------|-------|---------|
| `src/ir.h` | 1 | IR data structures |
| `src/ir.c` | 1 | IR construction helpers |
| `src/ir_build.c` | 1 | AST → IR translation |
| `src/emit_qbe.c` | 1 | IR → QBE IL text |
| `src/cfg.c` | 2-3 | CFG construction, dominator tree, dominance frontiers, SSA rename |
| `src/emit_llvm.c` | 5 | IR → LLVM IR text (in progress) |
| `src/ir_opt.c` | 4 | Scalar optimization passes |
| `docs/llvm-backend.md` | 5 | LLVM backend implementation plan |
| `tests/ir/` | 1-4 | IR-level test cases |

### Modified files
| File | Phase | Changes |
|------|-------|---------|
| `src/main.c` | 1 | Add `--backend` flag parsing |
| `CMakeLists.txt` | 1 | Add new source files |

---

## Learning Roadmap (aligned with reading guide)

| Phase | Topics | Guide Weeks |
|-------|--------|-------------|
| Phase 0 | Control flow (short-circuit, break/continue) | — (COMPLETE) |
| Phase 1 | Three-address code, IR design, AST→IR, QBE emission | 1 |
| Phase 2 | CFG construction, dominator tree, dominance frontiers | 2-4 |
| Phase 3 | SSA construction (Braun et al. 2013) | 5-8 |
| Phase 4 | DCE, constant propagation, constant folding | 9-10 |
| Phase 5 | Register allocation, instruction selection (future) | 12-18 |
