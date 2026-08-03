# TODO

## Current: Cranelift Backend (Phase 6)

LLVM backend done (40/40). Next: emit the same IR/SSA pipeline to Cranelift.
Plan in `docs/cranelift-backend.md`.

- [ ] **Research Cranelift API** — how to feed SSA IR (func preamble, params,
      blocks, insts) and emit ARM64 machine code on macOS without LLVM.
- [ ] **`src/emit_cranelift.c`** — IR → Cranelift builder.
- [ ] **`--backend=cranelift`** + wiring (main.c, CMakeLists, test_runner).
- [ ] **Verify** — 40/40 on qbe + aarch64-legacy + llvm + cranelift.

---

## Completed — LLVM IR Backend (Phase 5)

Emit LLVM IR from the IR/SSA pipeline and assemble with `llc`. Details in
`docs/llvm-backend.md`.

- [x] **`src/emit_llvm.c`** — LLVM IR emitter (`emit_llvm_operand`,
      `emit_llvm_instr`, `emit_llvm_function`, globals/strings emission).
- [x] **Call emission** — externs/functions signature resolution; all-i64
      declares + calls (ARM64 ABI); self-consistent call types; variadic via
      `...` when a call exceeds fixed params.
- [x] **`--backend=llvm`** — `BACKEND_LLVM` in `main.c` + `usage()`.
- [x] **`CMakeLists.txt`** — add `src/emit_llvm.c` + `LLC` detection.
- [x] **`test_runner.cmake`** — llvm branch: `.ll` → `llc -O1` → `.s` → `.o` → `.x`.
- [x] **Verify** — 40/40 on qbe + aarch64-legacy + llvm.

### Gotchas found along the way

- Extern param lists parse as `AST_LIST` (not `AST_ARRAY`); the ir_build check
  was wrong, so function externs were silently stored as extern *variable*
  globals (masked on QBE). Fixed in `ir_build.c` (distinguish `[]` index-access).
- LLVM c-strings don't decode `\n`/`\t`/`\x`; strings are emitted as explicit
  `[i8 ...]` byte arrays with exact decoded length.
- Block refs need `%`; `ptrtoint` constant exprs need parens; variadic args need
  explicit types; call type fixed-count must equal arg count.
- LLVM 21 rejects `%0` param shadowing — keep source param names.
- Extern-variable globals are skipped entirely (redefine same-file `let`
  globals otherwise).

## Completed — AST → IR → QBE → SSA pipeline

**Phases 0–3 done** (`docs/codegen-rework-plan.md`):
- Phase 1: AST → IR → QBE (full language coverage) — 40/40
- Phase 2: CFG construction, dominator tree, dominance frontiers
- Phase 3: SSA construction (dominator-tree rename; phis need liveness, not in pipeline)
- SSA runs before emission via `cfg_ssa_pipeline`; `--dump-cfg`, `--dump-ssa` flags

### Phase 4 — Scalar Optimizations (on SSA)

- [ ] **Constant folding** — evaluate `const op const` at compile time.

- [ ] **Constant propagation** — propagate known constants through `mov` and `phi`.

- [ ] **Dead code elimination** — remove instructions whose results are never used.

- [ ] **Copy propagation** — eliminate redundant `mov x = y` by substituting uses of `x`
  with `y` (within dominance constraints).

- [ ] **Strength reduction** — `mul by power of 2` → `lsl`, `div by power of 2` → `asr`.

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
