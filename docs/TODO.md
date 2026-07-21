# TODO

## Current: New Codegen Pipeline

Replace the monolithic `compiler.c` (1700 lines of direct AST→ARM64 emission) with a
proper IR pipeline: **AST → IR → CFG → DFA → SSA → scalar opts → backend**.

### Phase 1 — IR & CFG

- [ ] **Design IR instruction set** — three-address code,SSA-ready. Ops: `mov`, `load`,
  `store`, `add`/`sub`/`mul`/`div`/`mod`, `cmp`/`br`, `call`, `ret`, `phi`, `alloc`.
  Types: `i64`, `ptr`, `void`. Each IR instruction has exactly one result (or none for
  side-effect ops).

- [ ] **Basic block / CFG data structures** — `IRBlock` (id, list of instructions,
  predecessors, successors), `IRFunction` (name, param list, block list, symbol table).

- [ ] **IR builder** — walk the AST, emit IR into basic blocks. Start with straight-line
  code (expressions, assignments, let bindings). Handle `if`/`while` by splitting into
  blocks with terminator branches.

- [ ] **CFG construction** — wire up predecessor/successor edges as blocks are created.
  Verify single-entry/multiple-exit structure.

- [ ] **IR printer** — text dump for debugging (like LLVM's IR format). Replace the ARM64
  `.s` stdout with IR dump as first milestone.

### Phase 2 — SSA Construction

- [ ] **Dominance tree** — compute immediate dominators for each function's CFG.

- [ ] **Φ insertion** — place φ-nodes at dominance frontiers for every variable with
  multiple reaching definitions.

- [ ] **Variable renaming** — version variables along dominance tree traversal.
  Each version maps to a unique SSA name.

- [ ] **SSA verifier** — sanity check: every `load` dominates all uses, every `phi` is at
  a proper frontier, no undefined variable references.

### Phase 3 — Scalar Optimizations (on SSA)

- [ ] **Constant folding** — evaluate `const op const` at compile time.

- [ ] **Constant propagation** — propagate known constants through `mov` and `phi`.

- [ ] **Dead code elimination** — remove instructions whose results are never used.

- [ ] **Copy propagation** — eliminate redundant `mov x = y` by substituting uses of `x`
  with `y` (within dominance constraints).

- [ ] **Strength reduction** — `mul by power of 2` → `lsl`, `div by power of 2` → `asr`.

### Phase 4 — Backend: ARM64/macOS

- [ ] **Register allocator** — simple graph-coloring or linear-scan. 12 GP registers
  (x0-x15 callee-saved is x19-x28 + x29/x30), 8 arg registers (x0-x7). Handle spills
  to stack.

- [ ] **Instruction selection** — map IR ops to ARM64 instructions. Peephole patterns
  for common idioms (load/store offsets, fused shifts, etc.).

- [ ] **Stack frame layout** — variable placement, alignment, frame pointer setup. Handle
  large frames (>4095 bytes) with register-indirect sub.

- [ ] **Calling convention** — Darwin ARM64 ABI: named args in x0-x7, variadic args on
  stack. Spill layout must match `va_start` expectations (named arg NOT at `[sp, #0]`).

- [ ] **String literals** — `.data` section emission with `adrp`/`add` addressing.

- [ ] **External C functions** — `_` prefix, resolve from libc/libSDL at link time.

### Phase 5 — Backend: LLVM IR or QBE

- [ ] **QBE backend** — map IR to QBE's SSA-like IR text format, pipe to `qbe` compiler.
  Simpler than LLVM, good first target.

- [ ] **LLVM backend** (optional) — map IR to LLVM C API or textual IR. Full optimization
  pipeline for free, but heavier dependency.

- [ ] **Multi-backend dispatch** — CLI flag to select backend (`--backend arm64|qbe|llvm`).

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

## Cleanup

- [ ] **`SYM_STR` / `SYM_POINTER` overlap** — `str` and `*char` are the same thing but
  tracked as different symbol types. Unify.

- [ ] **Hardcoded struct/array sizes in arg passing** — `"// assuming 2 fields for now"` and
  `"// assuming 4 elements for now"` in the function call argument loop.

- [ ] **String arg handling copy-pasted** — `AST_STRING` in argument lists is handled with
  the same `strdup`/capacity-check block in at least two places.

- [ ] **Ret value paths** — verify `ret` works correctly in all control flow positions
  (nested if/while, end of function, etc.).

---

## Multi-File Compilation

- [x] **macOS `_` prefix for Clumsy functions** — labels and calls now use `_` prefix
  for ABI compliance. Cross-file linking works.

- [x] **Directory-based examples** — `examples/<name>/` directories with multiple `.cpl`
  files are compiled and linked together.

- [x] **extern declarations for Clumsy functions** — use `(extern func-name [args] ret)`
  to reference functions from other files.
