# Cranelift Backend — Implementation Notes (Phase 6)

Emit the post-SSA IR as **CLIF text** (Cranelift IR), compile it to an aarch64
Mach-O `.o` with `tools/clifc` (cranelift 0.134.3), and link with `clang`.
All four backends pass 40/40: `qbe`, `aarch64-legacy`, `llvm`, `cranelift`.

## Toolchain

- Rust **stable 1.92.0** at
  `~/.rustup/toolchains/stable-aarch64-apple-darwin/bin` — add to `PATH`:
  `export PATH="$HOME/.rustup/toolchains/stable-aarch64-apple-darwin/bin:$PATH"`
- Cranelift crates at **0.134.3**: `cranelift-codegen`, `cranelift-reader`,
  `cranelift-object`, `cranelift-module`
- No `cranelift-tools` / `clif-util` on crates.io — hence `tools/clifc`
- Build: `cargo build --release --manifest-path tools/clifc/Cargo.toml`

## Pipeline

```
.cpl ──clumsyc --backend=cranelift──▶ .clif   (CLIF text)
.clif ──tools/clifc─────────────────▶ .o      (aarch64 Mach-O)
.o    ──clang -o prog.x [libs]──────▶ executable
```

`tools/clifc/src/main.rs` reads `.clif` with `cranelift-reader`, emits a Mach-O
object via `ObjectModule` + `object::write::MachO`. For variadic externs it also
writes `{output}.stubs.s` (see below); `test_runner.cmake` links it when
present.

## IR → CLIF mapping

All values are `i64` (pointers included — no inttoptr/ptrtoint needed).

| IR | CLIF |
|----|------|
| `IR_ADD/SUB/MUL` | `iadd` / `isub` / `imul` |
| `IR_DIV/MOD` | `sdiv` / `srem` |
| `IR_AND/OR/XOR` | `band` / `bor` / `bxor` |
| `IR_SHL` | `ishl` |
| `IR_SHR` | **`ushr`** (unsigned; `ashr` → `sshr`) |
| `IR_NOT` | `bnot` |
| `IR_NEG` | `ineg` |
| `IR_EQ/NE` | `icmp eq` / `icmp ne` → `uextend.i64` |
| `IR_LT/GT/LE/GE` | `icmp slt/sgt/sle/sge` → `uextend.i64` |
| `IR_LOAD` | `load.i64 vaddr` (plain i64 address) |
| `IR_STORE` | `store.i64 vvalue, vaddr` |
| `IR_ALLOC` | `explicit_slot` + `stack_addr.i64` (see hoisting below) |
| `IR_JMP` | `jump blockN` |
| `IR_JNZ` | `brif vcond, blockT, blockE` |
| `IR_RET` | `return` |
| `IR_CALL` | `call fnN(v...)` |
| `IR_PHI` | **block params** — `jump blockN(vX)` / `brif ..., blockN(vX)` + `blockN(vX: i64):` |
| `IR_MOV` | rename the vreg (no copy opcode) |
| `IR_ADDR` | `symbol_value.i64 gvN` |

### Notes

- **Opcode renames vs QBE**: `brnz` → `brif`, `shr` → `ushr`.
- **Entry block params** must match the function signature exactly
  (`block0(vP: i64):` for each function param).
- **Calls**: preamble `fn%d = colocated u0:%d (i64,...)` references. The `fn%d`
  preamble index is the order in `fn_refs` (first-reference order), **not** the
  `u0:` FuncId. `u0:N` FuncIds are file order over all functions (explicit
  externs, implicit externs, own functions). DataIds (`u1:N`) are `;; data`
  file order.
- **Extern data**: preamble `gv%d = symbol colocated u1:%d`. String labels
  (`str_N`) are recognized as data via `find_string_value`; `printf` (implicit
  extern call target) is forced to be a function via `force_func` in
  `scan_operand_refs`/`pre_scan_refs`.
- **Stack slots**: `ss%d = explicit_slot %d`. All `stack_addr.i64` lines are
  hoisted into the entry block so definitions dominate all uses (cranelift's
  strict verifier requires this; QBE tolerates per-block placement). `IR_ALLOC`
  itself emits nothing — `alloc_vregs[slot]` records the vreg number.

## Variadic calls (printf) — the stub bridge

Apple AArch64's variadic ABI passes varargs **on the stack**
(`mov x9, sp; str x8, [x9]`), not in x-registers, and clang does **not** zero
`x8`. Cranelift 0.134.3 has **no variadic support**, so fixed `printf` calls
(signed as 8 fixed i64 params) are routed through a stub:

1. Emitter marks variadic externs (`;; variadic <name>` marker lines) and
   names the CLIF function `<name>_stub` (empty `{}` body).
2. `clifc` scans the markers and writes `{output}.stubs.s` with
   `_<name>_stub:` — saves x29/x30, reserves 64 bytes, stores x1..x7 to
   `[sp,0..48]`, sets `x8=#0`, `bl _<name>`, unwinds, `ret`.
3. `test_runner.cmake` appends `{TEST_NAME}.stubs.s` to the link when present.

Verified end-to-end: `test_if` prints "1" (was "5" before the stub fix).

## Key bugs found & fixed

- **JNZ arg layout**: IR_JNZ is `args[0]=cond, args[1]=then, args[2]=else`
  (matching emit_llvm) — emitter previously read `args[1..3]` and crashed.
- **Data vs function classification**: implicit extern `printf` was classified
  as data (zeroed bytes) and string labels weren't recognized as data. Fixed
  with `force_func` + `find_string_value`.
- **Call preamble reference**: `fn%d` used the `u0:` FuncId instead of the
  `fn_refs` index — undefined-function / mismatched-arg verifier errors.
  Fixed with `fn_for(e, fname)` + fallback.
- **Block numbering**: `e->block_num[i]` was indexed by DFS emission *position*
  instead of the block's index in `fn->blocks`, so `blockN(...)` headers
  disagreed with branch targets — "mismatched argument count" on brif/jump.
  Fixed by looking up the real block index (`block_index_of`) per emitted block.

## Definition of done (all ✅)

- [x] `tools/clifc` produces a linkable aarch64 Mach-O from CLIF text
- [x] `./build-clif/clumsyc --backend=cranelift tests/test_factorial_recursive.cpl`
      runs and prints `120`
- [x] `ctest --test-dir build-clif` 40/40
- [x] all four backends green (qbe, aarch64-legacy, llvm, cranelift)
