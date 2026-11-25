# Memory Pool Design

This document analyses the current ad-hoc `malloc`/`free`/`strdup` patterns across the
`clumsy` compiler and proposes replacing them with two lightweight **bump-pointer arenas**
that match the natural object lifetimes of the compilation pipeline.

## Background: Current Allocation Patterns

Almost every allocation in the compiler belongs to one of three lifetime groups:

| Lifetime | Objects | Current cleanup |
| :--- | :--- | :--- |
| **Whole compilation** | Tokens, AST nodes, symbol tables, IR program/functions/blocks, IR operand strings | `free_token_array`, `free_ast`, `free_symbol_table`, `free_ir_program` |
| **Per-function / per-pass** | CFG scratch arrays (`visited`, `rpo`, `worklist`), SSA rename stacks, emitter sub-arrays | `free()` at end of each pass function |
| **Per-expression (temp)** | `ir_gen_temp()` / `ir_gen_label()` strings in `ir_build.c` | `free(temp)` immediately after use |

The third group is the most wasteful. The pattern appears 30+ times in `ir_build.c`:

```c
// 1) malloc + sprintf  → "t17"
char *temp = ir_gen_temp(b);
// 2) op_reg() calls ir_strdup() → second malloc + strcpy of same string
ir_emit(b, ir_instr(IR_ADD, op_reg(temp), left, right, op_none(), IR_TYPE_I64));
// 3) free the original
free(temp);
```

That is **2 `malloc` + 1 `free`** per temporary register name. A single function body
can produce hundreds of these triples.

## Hotspot Inventory

### `tokenizer.c`

- `malloc` + `free` for scratch buffers (comment, string, number, identifier) — one pair
  per token, immediately discarded after `strdup` stores the value.
- `strdup` inside `create_token` for `token.value` — survives until `free_token_array`.
- `realloc` on `TokenArray.tokens` — O(log N) reallocations.

### `parser.c`

- `malloc(sizeof(ASTNode))` per node via `create_ast_node`.
- `strdup` per string-valued node (identifiers, operators, keywords).
- `realloc` on `list.children` and `fn.params` — O(log N) per list.

### `ir.c`

- `calloc` × 5 per `IRBlock` (label, instrs, succs, preds, dom_frontiers).
- `ir_strdup` per `IROperand` string (register name, block label, global name).
- `calloc` / `realloc` for `IRFunction.blocks`, `IRProgram.functions`, `.externs`,
  `.globals`, function string literal arrays.

### `ir_build.c`

- `ir_gen_temp` → `ir_strdup("tN")` — scratch, freed immediately after `op_reg`.
- `ir_gen_string_label` → `ir_strdup("str_N")` — same.
- `ir_gen_label` → `ir_strdup` — freed immediately after `ir_add_block`.
- `ir_add_var` stores `ir_strdup` copies of name and register — lives for the function.

### `cfg.c`

- `calloc` for `visited[]`, `rpo[]`, `stack[]` in RPO — freed at end of `compute_rpo`.
- `calloc` for `worklist[]`, `in_worklist[]`, `has_phi[]` — freed per variable in
  `cfg_insert_phi_nodes`.
- `calloc` for `VarStack[]` and per-stack `versions[]` — freed at end of
  `cfg_rename_variables`.
- `ir_strdup(buf)` for renamed `%v_N` registers — stored in live IR operands.

### `emit_cranelift.c` / `emit_llvm.c`

- `calloc` × 8 for `ClifEmitter` sub-arrays (block_order, block_num, slot_sizes,
  alloc_vregs, alias, implicit, variadic, gv_refs, fn_refs).
- `calloc(cap, 1)` for a per-function `bytes` buffer.
- `ir_strdup` for `gv_refs[].name`, `fn_refs[].name`, `implicit[]`, `dl.items[].name/str_val`.
- All freed in a matching block at the end of `emit_cranelift` / `emit_llvm`.

## Proposed Design: Two Arenas

### Concept

Replace all allocations with two bump-pointer arenas whose lifetimes match the groups
identified above.

```
┌─────────────────────────────┐   ┌────────────────────────────┐
│  COMPILE ARENA              │   │  SCRATCH ARENA             │
│  (whole-program lifetime)   │   │  (per-function / per-pass) │
├─────────────────────────────┤   ├────────────────────────────┤
│  TokenArray + token values  │   │  ir_gen_temp() strings     │
│  ASTNode tree               │   │  ir_gen_label() strings    │
│  SymbolTable tree           │   │  CFG: visited, rpo, stack  │
│  IRProgram / IRFunction /   │   │  CFG: worklist, has_phi    │
│    IRBlock / IRInstr arrays │   │  VarStack[] + versions[]   │
│  IROperand strings          │   │  Emitter: bytes[], fn_refs │
│  Global/extern metadata     │   │    gv_refs[], block_order  │
└─────────────────────────────┘   └────────────────────────────┘
  freed once at process exit         reset between functions / passes
```

### Arena API (`src/arena.h` / `src/arena.c`)

A minimal bump-pointer arena backed by a singly-linked list of fixed-size chunks.

```c
typedef struct ArenaChunk {
    struct ArenaChunk *next;
    size_t capacity;
    size_t used;
    char   data[];          // flexible array member
} ArenaChunk;

typedef struct {
    ArenaChunk *head;       // most-recently-allocated chunk
    size_t      chunk_size; // default size for new chunks
} Arena;

void  arena_init   (Arena *a, size_t chunk_size);
void *arena_alloc  (Arena *a, size_t size);  // bump pointer, O(1)
char *arena_strdup (Arena *a, const char *s); // arena-backed strdup
void  arena_reset  (Arena *a);  // bulk-free, retain chunks for reuse
void  arena_destroy(Arena *a);  // free all chunks
```

**Properties:**
- Allocation is O(1): advance a pointer within the current chunk; allocate a new chunk
  only on overflow.
- Alignment: always align to `_Alignof(max_align_t)` (16 bytes on AArch64).
- `arena_reset` zeroes `used` on each chunk but keeps the memory — ideal for the scratch
  arena, which is reset per function.
- No per-object `free`. Cleanup is a single `arena_destroy` call.

### Suggested initial sizes

| Arena | Initial chunk | Rationale |
| :--- | :--- | :--- |
| Compile arena | 2 MB | Covers tokens + AST + all IR for typical programs |
| Scratch arena | 64 KB | Covers all temp strings + CFG pass arrays for one function |

## Mapping Current Calls to Arena Calls

### Compile arena (`ca`)

| Current | Replacement |
| :--- | :--- |
| `malloc(sizeof(TokenArray))` | `arena_alloc(ca, sizeof(TokenArray))` |
| `strdup(value)` in `create_token` | `arena_strdup(ca, value)` |
| Scratch buf + `strdup` per token | Stack VLA for scratch; `arena_strdup(ca, buf)` in `create_token` |
| `malloc(sizeof(ASTNode))` | `arena_alloc(ca, sizeof(ASTNode))` |
| `strdup(...)` in parser | `arena_strdup(ca, ...)` |
| `calloc(1, sizeof(IRBlock))` | `arena_alloc(ca, sizeof(IRBlock))` + `memset` |
| `ir_strdup(label)` for operand strings | `arena_strdup(ca, label)` |
| `calloc(cap, sizeof(IRInstr))` | `arena_alloc(ca, cap * sizeof(IRInstr))` |
| `ir_strdup` for renamed SSA registers | `arena_strdup(ca, buf)` |

### Scratch arena (`sa`)

| Current | Replacement |
| :--- | :--- |
| `ir_gen_temp(b)` | `arena_strdup(sa, buf)` |
| `ir_gen_label(fn, prefix)` | `arena_strdup(sa, buf)` |
| `free(temp)` / `free(label)` | **deleted** |
| `calloc(n, ...)` for CFG pass arrays | `arena_alloc(sa, n * sizeof(...))` |
| `calloc(num_vars, sizeof(VarStack))` | `arena_alloc(sa, ...)` |
| `calloc(e.gv_refs_cap, ...)` in emitters | `arena_alloc(sa, ...)` |
| Per-function `bytes` buffer in cranelift emitter | `arena_alloc(sa, cap)` + `arena_reset(sa)` between functions |

### Example: `ir_build.c` before and after

```c
// BEFORE — 2 malloc + 1 free per temp:
char *temp = ir_gen_temp(b);
ir_emit(b, ir_instr(IR_ADD, op_reg(temp), left, right, op_none(), IR_TYPE_I64));
free(temp);

// AFTER — 1 arena bump for temp; op_reg stores to compile arena:
char *temp = ir_gen_temp(b);          // arena_strdup(b->scratch, "tN")
ir_emit(b, ir_instr(IR_ADD, op_reg(temp), left, right, op_none(), IR_TYPE_I64));
// no free — scratch arena is reset at function boundary
```

## Handling `realloc`-Grown Arrays

`realloc` is incompatible with bump-pointer arenas because the arena cannot reclaim
the old backing store. The growing arrays in `ir.c` and `cfg.c` use two strategies:

### Option A — Pre-sized (recommended)

Allocate at a generous initial capacity. If growth is still needed, allocate a larger
block from the arena and `memcpy` the old data. The old block is "wasted" in the arena
but bounded (at most equal to the final array size).

| Array | Suggested initial capacity |
| :--- | :--- |
| `IRBlock.instrs` | 32 |
| `IRFunction.blocks` | 16 |
| `IRProgram.functions` | 8 |
| `IRProgram.externs` | 8 |
| `IRProgram.globals` | 16 |
| `IRBuilder.vars` | 128 |

### Option B — Linked-list of fixed chunks

Embed a `next` pointer in each array's backing store. More complex; zero wasted memory.
Not recommended unless wasted memory is measured to be a problem.

## Impact Estimate

Measured against a ~1,000-line `clumsy` source file:

| Metric | Before | After |
| :--- | :--- | :--- |
| `malloc` / `calloc` calls | ~15,000–20,000 | ~200–500 (chunk allocs only) |
| `free` calls | ~12,000–18,000 | 2 (`arena_destroy` × 2, or none on exit) |
| `strdup` calls | ~5,000–8,000 | 0 (replaced by `arena_strdup`) |
| Compile-time speedup (est.) | — | 5–15% |

For a compiler that runs and exits immediately, skipping `arena_destroy` and letting the
OS reclaim the address space is a valid further simplification.

## Migration Order

Each step is independently buildable and testable with `ctest`.

1. **`src/arena.h` + `src/arena.c`** — implement and unit-test the arena.
2. **`src/main.c`** — initialise two arenas; expose as global or pass through pipeline.
3. **`src/tokenizer.c`** — migrate token values to compile arena; remove scratch bufs;
   hollow out `free_token_array`.
4. **`src/parser.c`** — migrate AST nodes and string values; hollow out `free_ast`.
5. **`src/compiler.c`** — migrate `SymbolTable`, `CodeGen`, `StructTypeTable`; hollow
   out `free_symbol_table`, `free_struct_type_table`, `free_codegen`.
6. **`src/ir.c`** — migrate `IRProgram` / `IRFunction` / `IRBlock`; hollow out
   `free_ir_program` and helpers.
7. **`src/ir_build.c`** — point temp generation at scratch arena; delete all
   `free(temp)` / `free(label)` / `free(addr_temp)` calls; reset scratch at function
   boundary.
8. **`src/cfg.c`** — CFG scratch arrays to scratch arena; renamed register strings to
   compile arena.
9. **`src/emit_cranelift.c`**, **`src/emit_llvm.c`**, **`src/emit_qbe.c`** — emitter-local
   arrays to scratch arena; reset scratch between functions.

## Files Affected

| File | Change |
| :--- | :--- |
| `src/arena.h` | **New** — arena interface |
| `src/arena.c` | **New** — bump-pointer arena implementation |
| `src/main.c` | Initialise / destroy arenas |
| `src/tokenizer.c` | Use compile arena; remove scratch buffers |
| `src/parser.c` | Use compile arena; hollow out `free_ast` |
| `src/compiler.c` | Use compile arena; hollow out `free_*` bodies |
| `src/ir.c` | Use compile arena; hollow out `free_ir_*` bodies |
| `src/ir_build.c` | Temps use scratch arena; delete all `free(temp)` calls |
| `src/cfg.c` | Pass arrays use scratch arena |
| `src/emit_cranelift.c` | Emitter arrays use scratch arena |
| `src/emit_llvm.c` | Emitter arrays use scratch arena |
| `src/emit_qbe.c` | Minor temp string cleanup |
| `src/ir.h` | `ir_strdup` accepts arena param |
| `src/types.h` | `free_*` prototypes become stubs (or removed) |
| `CMakeLists.txt` | Add `src/arena.c` to source list |
