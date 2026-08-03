# LLVM Backend — Implementation Notes

Emit LLVM IR text from the in-house IR (post-SSA), pipe through `llc` (LLVM 21,
Homebrew) to ARM64 assembly. Mirrors `emit_qbe.c` structure.

Toolchain: `/opt/homebrew/opt/llvm/bin/llc` — **LLVM 21.1.8** (opaque `ptr`
types, no typed pointers). llc adds the leading `_` to global symbols
automatically on Mach-O.

---

## Approach

Keep the IR's "everything is an integer" model: all values (including pointer
values from `IR_ALLOC`/`IR_ADDR`) are `i64`. Convert at the boundary with
`inttoptr`/`ptrtoint`:

| IR value | LLVM |
|----------|------|
| integer value | `i64` |
| address of global/string/stack slot | `i64` (via `ptrtoint`) |
| memory operand in load/store | `ptr` (via `inttoptr i64 → ptr`) |
| extern call arguments | `i64` (never converted — see Extern Calls) |

LLVM's optimizer folds away the casts.

---

## Instruction Mapping

| IR | LLVM IR |
|----|---------|
| `IR_ADD` | `%x = add i64 %a, %b` |
| `IR_SUB` | `%x = sub i64 %a, %b` |
| `IR_MUL` | `%x = mul i64 %a, %b` |
| `IR_DIV` | `%x = sdiv i64 %a, %b` |
| `IR_MOD` | `%x = srem i64 %a, %b` |
| `IR_AND` | `%x = and i64 %a, %b` |
| `IR_OR`  | `%x = or  i64 %a, %b` |
| `IR_XOR` | `%x = xor i64 %a, %b` |
| `IR_SHL` | `%x = shl i64 %a, %b` |
| `IR_SHR` | `%x = ashr i64 %a, %b` (signed) |
| `IR_NOT` | `%x = xor i64 %a, -1` |
| `IR_NEG` | `%x = sub i64 0, %a` |
| `IR_EQ` | `%c = icmp eq i64 %a, %b` + `%x = zext i1 %c to i64` |
| `IR_NE` | `icmp ne` |
| `IR_LT` | `icmp slt` |
| `IR_GT` | `icmp sgt` |
| `IR_LE` | `icmp sle` |
| `IR_GE` | `icmp sge` |
| `IR_LOAD` | `%p = inttoptr i64 %a to ptr` + `%x = load i64, ptr %p` |
| `IR_STORE` | `%p = inttoptr i64 %a to ptr` + `store i64 %v, ptr %p` |
| `IR_ALLOC` | `%t = alloca [N x i8], align 16` + `%x = ptrtoint ptr %t to i64` (bytes = args[0], padded to 16) |
| `IR_JMP` | `br label %B` |
| `IR_JNZ` | `%c = icmp ne i64 %cond, 0` + `br i1 %c, label %T, label %E` |
| `IR_RET` | `ret i64 %v` (or `ret void`) |
| `IR_CALL` | `call i64 @f(...)` (see below) |
| `IR_PHI` | `%x = phi i64 [%v1, %b1], [%v2, %b2]` |
| `IR_MOV` | `%x = or i64 %src, 0` (LLVM has no `copy`; folds to identity) |
| `IR_ADDR` | `%x = ptrtoint ptr @g to i64` (or `inttoptr`/`ptrtoint` round-trip for non-globals) |

### Notes

- **`IR_ALLOC` size**: structs may be non-multiple-of-16; always pad to 16:
  `((size + 15) / 16) * 16`. QBE emits exact bytes, LLVM aligns to 16.
- **`IR_JNZ`**: LLVM needs `i1`; always emit `icmp ne` first.
- **Block references** need the `%` prefix: `br label %block`; block *definitions*
  are bare `block:`.
- **LLVM 21 requires** params keep their source names — `%0`-style shadowing of
  parameters is broken on 21 (works on 18/19). All params use `%p0`, `%p1`, …
- **`main` returning `i64`** works on macOS; only the low byte of the exit
  status is observed.

---

## Extern Calls

`args[0]` = callee (always `OP_GLOBAL`). Remaining args = actual args.

**Callee signature** is resolved by name: `prog->externs` → `num_params`,
`is_variadic`, `return_type`; else `prog->functions`; else the builtin `printf`
fallback (print/println). All IR values are `i64`, so **externs are declared and
called all-i64 regardless of their C signature** — ARM64 passes `i64` and `ptr`
identically in x0–x7, and llc does not cross-check a call's type against its
`declare`.

```
declare i64 @puts(i64)
declare i64 @printf(i64, i64, ...)     ; ... iff is_variadic or a call site exceeds nparams
declare void @SDL_Quit(i64)

call i64 (i64) @puts(i64 ptrtoint (ptr @str_0 to i64))
call i64 (i64, ...) @printf(i64 ptrtoint (ptr @str_0 to i64), i64 %x)
```

- **Call type must be self-consistent**: llc rejects a call whose type's fixed
  param count ≠ its argument count. Rule: own functions → fixed = `nparams`;
  variadic extern → fixed = `nparams` + `...`; non-variadic extern → fixed =
  `nargs` (clamp down if fewer args than declared).
- **Variadic args require explicit types**; fixed args tolerate them. Always
  emit `i64 <arg>`.
- **Global (pointer) args**: `OP_GLOBAL` operands in calls become
  `ptrtoint (ptr @g to i64)` — constant-expression form with parens, and it must
  be an `i64`-typed arg.
- **Excess args** beyond declared fixed params make an extern call variadic at
  both the call and its `declare`.

### Extern detection in IR build

Function externs `(extern puts [*char] int)` are stored in `prog->externs`; the
parser builds their param list as an `AST_LIST` (ir_build originally checked
`AST_ARRAY`, so every function extern silently became an extern *variable*
global — masked on QBE, which never emits declares). The check now accepts an
`AST_LIST` whose first child is not the `[]` index-access operator (which is how
`int[4]` type nodes parse). Variable externs `(extern board int[4])` remain
`is_extern` globals.

---

## Globals & Strings

Order: extern declares, then globals + strings, then functions. `OP_GLOBAL`
operands reference these by name (no `_` prefix mangling; llc/clang add it at
link time, same as QBE path).

### String literals (function-local pool + `str` globals)

LLVM's `c"..."` escape support is **limited**: only `\\`, `\"` and octal
(≤2 digits, so max byte 63). It does NOT decode `\n`/`\t`/`\x`. The IR stores
source text verbatim (`"\n"` is two chars backslash+n), so strings are decoded
to bytes and emitted as **explicit byte arrays**:

```
@str_0 = private unnamed_addr constant [7 x i8] [i8 68, i8 111, i8 110, i8 101, i8 46, i8 10, i8 0]
```

- length = decoded byte count + 1 for the NUL (must match exactly, or llc
  errors "constant expression type mismatch")
- function-local strings: `private unnamed_addr`; public `str` globals: external
  linkage (no keyword)

### Scalar globals

```
@g = internal global i64 5        ; file-private
@g = global i64 5                 ; pub (external default)
```

### Array globals

```
@g = global [10 x i64] [i64 0, i64 0, ...]
```

- `array_values` if present, else fill with `value`.

### Extern variables

`is_extern` globals are **skipped entirely** (QBE does the same). Emitting
`@g = external global i64` redefines a same-file `let`-defined global
(`(let board ...)` + `(extern board ...)` → `@board` redefinition). Cross-file
variable externs are not covered by tests.

### Struct type metadata

Skip `is_struct` globals entirely (QBE does the same). Struct instances are
plain stack allocs / global arrays; field offsets are computed at IR-build
time.

---

## Functions

```
define i64 @name(i64 %p0, i64 %p1) {
entry:
  ...
}
```

- **pub** → no linkage keyword (`define i64 @name(...)`)
- **file-private** → `define internal i64 @name(...)` (keyword order matters)
- **`main`** → `define i64 @main(...)` — verified working on macOS.

**Blocks** → `%name:`. Sanitize LLVM identifiers: `[a-zA-Z0-9_$.-]`. Vregs
`%v_N`, temps `%_llN` — all valid LLVM ids already.

---

## Files & CLI

- `src/emit_llvm.c` — `void emit_llvm(IRProgram *prog, FILE *out)`; mirrors
  `emit_qbe.c`: operand / instr / function emitters, then globals/strings/externs
  at top.
- `src/ir.h` — `emit_llvm(IRProgram *prog, FILE *out);`
- `src/main.c` — `BACKEND_LLVM` enum, `--backend=llvm`, emit dispatch
- `CMakeLists.txt` — `src/emit_llvm.c`; `LLC` find_program with fallback
  `/opt/homebrew/opt/llvm/bin/llc`
- `test_runner.cmake` — `"llvm"` branch: `.ll` → `llc -O1` → `.s` → `clang` →
  `.o` → `.x`

---

## Verification

1. Build: `cmake -B build && cmake --build build`
2. Full suite, all three backends:
   `ctest --test-dir build` (40/40 on **qbe**, **aarch64-legacy**, **llvm**)
3. Hand pipeline:
   ```
   ./build/clumsyc --backend=llvm prog.cpl > /tmp/prog.ll
   /opt/homebrew/opt/llvm/bin/llc -O1 -o /tmp/prog.s /tmp/prog.ll
   clang -o /tmp/prog /tmp/prog.s
   /tmp/prog
   ```
4. Tetris: 4-file build through the llvm pipeline (`.ll` per file, `llc`, link
   with SDL2)
5. Compare assembly quality vs QBE for a couple of benchmarks (informational)

---

## Gotchas

- **JNZ**: comparisons produce `i1` via `icmp`, then `br i1`; `OP_VREG`
  conditions are `i64` → `icmp ne`.
- **Variadic calls**: each arg needs an explicit `i64` type; the fixed part of
  the call type must equal the arg count.
- **`ptrtoint` constant expressions** need parens: `ptrtoint (ptr @g to i64)`.
- **String bytes**: c-strings can't carry `\n`/`\t`/high bytes — use explicit
  `[i8 ...]` arrays; the byte count must match exactly.
- **`%0` param shadowing** breaks on LLVM 21 — keep source param names.
- **`bitcast` between identical types is invalid** — use `or %x, 0` for copies.
- **Extern function detection** in ir_build relies on `AST_LIST` param lists,
  not `AST_ARRAY` (see above).
