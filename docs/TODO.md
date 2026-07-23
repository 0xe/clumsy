# TODO

## Current: AST → IR → QBE Pipeline

Replace the monolithic `compiler.c` (AST → ARM64) with a proper IR pipeline: **AST → IR → CFG → SSA → scalar opts → backend**.

**Phase 0 complete.** Full plan documented in `docs/codegen-rework-plan.md`.

### Phase 1 — AST → IR → QBE (Full Language Coverage)

Build the complete IR pipeline for all language features in one pass. No Phase 1/Phase 2 split.

- [ ] **IR data structures** — `src/ir.h`, `src/ir.c`. IR instructions, operands, basic blocks, functions.
- [ ] **QBE emitter** — `src/emit_qbe.c`. IR → QBE IL text emission.
- [ ] **AST→IR translation** — `src/ir_build.c`. Walk AST, emit IR for all node types.
- [ ] **Backend selection** — Add `--backend=aarch64-legacy|qbe` flag to `src/main.c`.
- [ ] **Core verification** — Simple test programs compile through QBE, output matches legacy.
- [ ] **Full verification** — All 40 tests + tetris compile through QBE, output matches legacy.

### Phase 2 — CFG Construction + Dominator Analysis

- [ ] **Dominance tree** — compute immediate dominators for each function's CFG.

- [ ] **Dominance frontiers** — needed for phi insertion in Phase 3.

### Phase 3 — SSA Construction

- [ ] **Φ insertion** — place φ-nodes at dominance frontiers for every variable with
  multiple reaching definitions.

- [ ] **Variable renaming** — version variables along dominance tree traversal.
  Each version maps to a unique SSA name.

- [ ] **SSA verifier** — sanity check: every `load` dominates all uses, every `phi` is at
  a proper frontier, no undefined variable references.

### Phase 4 — Scalar Optimizations (on SSA)

- [ ] **Constant folding** — evaluate `const op const` at compile time.

- [ ] **Constant propagation** — propagate known constants through `mov` and `phi`.

- [ ] **Dead code elimination** — remove instructions whose results are never used.

- [ ] **Copy propagation** — eliminate redundant `mov x = y` by substituting uses of `x`
  with `y` (within dominance constraints).

- [ ] **Strength reduction** — `mul by power of 2` → `lsl`, `div by power of 2` → `asr`.

### Phase 5 — Future Backends

- [ ] **QBE backend** — map IR to QBE's SSA-like IR text format, pipe to `qbe` compiler.

- [ ] **LLVM backend** (optional) — map IR to LLVM C API or textual IR.

- [ ] **Native ARM64 backend** — register allocation, instruction selection, ABI compliance.

---

## Language Features

### Priority 1: Float/Double Support

- [ ] **Add `double` type** — new token `double`, parser support for double literals
  (e.g., `3.14`, `-0.5`, `1e10`).

- [ ] **FP registers** — use d0-d7 for argument passing, d0-d31 general purpose.
  ARM64 has separate integer and FP register files.

- [ ] **FP codegen** — emit `fmov`, `fadd`, `fsub`, `fmul`, `fdiv`, `fcmp` instructions.
  Handle int↔double conversions (`scvtf`, `fcvtzs`).

- [ ] **FP literals in string formatting** — `%f` support in printf (or new `print_double`
  helper).

### Priority 2: Type Checking Pass

- [ ] **Type table** — extend symbol table to track types more rigorously (not just
  string names). Support `int`, `double`, `(* T)` pointer types.

- [ ] **Type inference** — infer types for expressions and function return values.

- [ ] **Type checking pass** — separate pass before codegen that validates:
  - Argument types match function signatures
  - Arithmetic ops have correct operand types (no `int + double` without conversion)
  - Comparison ops return `int` (0 or 1)
  - Pointer types are compatible in assignments and calls
  - Array index types are `int`

- [ ] **Type error messages** — clear messages like "expected int, got double" with
  source location.

### Lower Priority

- [ ] **String operations** — concatenation, comparison, length. (Or just use C stdlib.)

---

## Developer Experience

- [ ] **Assembly comments with source** — emit equivalent Clumsy code as comments in the
  generated `.s` file. Each ARM64 instruction block gets a `//` comment showing the
  original s-expression. Makes debugging codegen much easier.

- [ ] **Port Speclite benchmarks** — port classic language benchmarks to Clumsy. Covers:
  fibonacci, ackermann, mandelbrot, nbody, spectralnorm, fannkuch, fasta, k-nucleotide,
  pidigits, sieve, chameneos-redux, etc. Good stress test for the new IR pipeline and
  a baseline for comparing codegen quality against C.
