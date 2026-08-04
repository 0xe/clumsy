# Completed Work

## Variadic `printf` calling convention (all backends)

On Apple's AArch64 ABI, variadic args go on the stack with the first at `[sp]`,
and `x8` holds the vector-arg count. QBE and LLVM's `...` lowering don't
produce this convention, and the legacy backend's register-only arg pass
dropped varargs onto the stack as garbage (only worked by accident).

- QBE / LLVM / Cranelift route variadic calls through fixed-arity `<name>_stub`
  shims (`src/stubs.c`, `--emit-stubs`, clifc for cranelift) that store
  `x1..x7` to `[sp,0..48]`, zero `x8`, and tail into the real function.
- Legacy backend restores the stack-spill scheme for variadic C calls: arg0 at
  the last slot, varargs at `[sp,#0]..`, block kept live across the call.
- Shared variadic rules in `src/ir.c` (`ir_call_is_variadic`,
  `ir_extern_is_variadic`, `ir_program_calls`) unify detection across emitters;
  stub emission only fires for externs the program actually calls (no duplicate
  symbols in multi-file examples).
- Regression test `tests/test_printf_variadic.cpl`; 42/42 on all four backends.

---

## Phase 0: Missing Control Constructs

### Logical Operators (short-circuit evaluation)
- `(&& a b)` — logical AND: returns 1 if both sides non-zero, short-circuits on first zero
- `(|| a b)` — logical OR: returns 1 if either side non-zero, short-circuits on first non-zero
- `(! expr)` — prefix logical not: returns 1 if expr is 0, else 0
- Codegen uses `cbz`/`cbnz` + label sequences for short-circuit evaluation

### Loop Control
- `break` keyword: exits innermost while loop (generates `b <end_label>`)
- `continue` keyword: jumps back to loop condition (generates `b <loop_label>`)
- `LoopContext` stack in `CodeGen` tracks break/continue targets for nested loops
- Error on `break`/`continue` outside of loop

---

## Cleanup & Syntax Revision (Phases 1–5)

### Test Suite Reorganization
- Demos moved to `examples/`: real_tetris, sdl_demo, sdl_loop, sdl_bindings, tcp_client, tcp_server, c_interop_demo
- Deleted obsolete tests; recreated `test_loop_basic.cpl`
- 34 tests retained, all with `.expected` files (except `test_c_interop` compile-smoke)
- Tests using broken `printf` varargs rewritten to use `print_int`/`print_str` helpers

### Tokenizer & Parser Syntax
- `*` and `&` always tokenize as `TOKEN_OPERATOR` via maximal-munch operator loop
- Pointer types: `(* int)` instead of `*int`
- Address-of: `(& x)` instead of `&x`
- Dereference: `(* ptr)` instead of `*ptr`
- `(include ...)` dropped — unnecessary since clumsy compiles to `.s` and links at object level

### Exponentiation
- `(** base exponent)` works via integer power helper function

### Compiler Strictness
- Parser `parse_exp` default case: syntax error with line number + exit
- Undeclared variables/functions: error + exit in all codegen paths

### Operators
- `=` equality comparison (replaced `==`)
- `not` logical negation `(not expr)` — evaluates to 1 if expr is 0, else 0
- Bitwise: `&` (AND), `|` (OR), `>>` (right shift), `<<` (left shift), `~` (bitwise NOT)

### AST_FN Node Type
- Dedicated `AST_FN` node with named fields: `fn.params`, `fn.param_count`, `fn.return_type`, `fn.body`
- Removed fragile `strcmp("fn")` string checks from parser, compiler, and symbol table construction

### Negative Integer Literals
- Tokenizer handles `-1`, `-100`, `-0xFF` as single `TOKEN_INT` tokens

### Examples & Build System
- All examples compile and link with proper SDL2/SDL2_net flags
- `print_helpers` as CMake STATIC library (not standalone `.o`)
- Per-example SDL link libs via CMakeLists.txt pattern matching
- Directory-based examples: `examples/<name>/` directories with multiple `.cpl` files

### Documentation
- `docs/DONE.md`: completed work log
- `docs/TODO.md`: prioritized list of remaining work

---

## Codegen Fixes (Before New Pipeline)

### Stack & Register Management
- `sp_displacement` tracks transient `stp`/`ldp` call frames (replaced `+16` magic constant)
- `emit_push_call_frame` / `emit_pop_call_frame` helpers replace raw stp/ldp patterns
- `emit_load_variable` and `emit_store_variable` use `sp_displacement` for automatic offset adjustment
- Binary expression register clobbering fixed via per-depth scratch slots (`scratch_offset`, `scratch_depth`)
- ARM64 large stack frames (>4095 bytes) use `emit_mov_immediate` + register-indirect `sub`/`add`

### Calling Convention
- Two-pass spill-slot approach in `emit_function_call` prevents arg register clobbering
- Darwin ARM64 variadic ABI fix: named args NOT at `[sp, #0]`, variadic args in overflow area starting at `[sp, #0]`
- macOS `_` prefix for all symbols (C functions and Clumsy-defined functions)

### Lexical Scoping
- Nested `SymbolTable` tree with `first_child` / `next_sibling` / `next_offset`
- `ASTNode.scope` links begin/if/while blocks to pre-built child scopes
- `total_tree_size()` recursively sums all non-function symbol sizes across scope tree

### Array & Struct Support
- Array stack allocation uses actual declared size (`size * 8`) instead of hardcoded 32 bytes
- `(set arr[i] val)` syntax for array element assignment (static and dynamic indices)
- Array zero-fill initialization for declared sizes
- Struct stack sizes use actual field count from `global_struct_types` instead of hardcoded 16 bytes

### String Literals
- `intern_string()` helper deduplicates string literal interning (pool store, quote stripping, `adrp`/`add` emission)

### Return Statements
- `ret` generates `b epilogue` jump to function exit instead of falling through
- Works correctly in `if` branches, `while` loops, and mid-block positions
- Both `_main` and user-defined functions get epilogue labels

### SDL Event Handling
- `SDL_PollEvent` via `int[7]` buffer (56 bytes matching `SDL_Event` union)
- Event type: `(& ev[0] 0xFFFF)` (lower 16 bits)
- Keycode: `(>> ev[2] 32)` (upper 32 bits of `ev[2]` = `SDL_Keysym.sym`)
- `SDL_PumpEvents` required each frame for macOS compositor

---

## Multi-File Compilation

- macOS `_` prefix for Clumsy function labels and calls (ABI compliance for cross-file linking)
- Directory-based examples: `examples/<name>/` directories compiled and linked together
- `extern` declarations work for both C functions and Clumsy functions from other files

---

## Multi-File Compilation

### `pub` keyword and symbol visibility
- `pub` keyword added to tokenizer keywords list (acts like `let` but sets `is_public`)
- `Symbol` struct has `is_public` (pub: exported with .globl) and `is_global` (top-level let/pub: stored in .data)
- `add_symbol` skips stack allocation for `is_global`/`is_external` symbols (sets `stack_offset = -1`)
- `total_scope_size` / `total_tree_size` exclude extern/global vars from stack frame size

### Global variables in `.data` section
- Top-level `let`/`pub` emit to `.data` section: `.word` for ints, `.asciz` for strings, `.quad` for arrays
- `.globl` only emitted for `pub` symbols
- `emit_load_variable` / `emit_store_variable` check `sym->is_global` and use `adrp`/`add` addressing
- `[]` array read for globals: uses `adrp`/`add` for base, index computed before base load to avoid register clobbering
- `set []` array write for globals: same pattern — index first, then base, then store
- `(& global)` address-of: uses `adrp`/`add` for global variables

### Extern for variables
- `(extern name type)` — function externs have `[params]` making 3+ children, variable externs have 2
- `stmt->data.list.count >= 3` distinguishes function externs from variable externs

### Multi-file build system
- `example_runner.cmake` rewritten: passes `-DEXAMPLE_DIR`, globs `.cpl` files, sorts with `main.cpl` first
- Each file compiled to `.o`, linked together with `clang`
- `generate_main_function` removed: dead code deleted from compiler.c and compiler.h
- All 40 tests wrapped in `pub main (fn [] int ...)` — all passing

---

## Tetris Multi-File Refactor

### File structure
- `config.cpl` — constants, colors, shapes (no SDL externs needed)
- `tetrominos.cpl` — game state, piece logic, input handling (pub functions)
- `render.cpl` — drawing helpers: `draw_cell`, `draw_board`, `draw_piece`, `draw_grid`, `render_frame`, `draw_game_over`
- `main.cpl` — init, game loop, cleanup (~60 lines)

### Local temp variables
- Removed shared globals `_i`, `_ok`, `_nx`, `_ny`, `base`
- Each function declares its own local `(let _i int 0)` where needed
- Real shared state remains global: `window`, `renderer`, `board`, `pk`, `pr`, `ppx`, `ppy`, `score`, `over`, `timer`, `ev`, `rc`, `cx`, `cy`

---

## Test Suite Audit (from review.md)

### Silent Code Omission (Fixed)
The compiler used to silently ignore expressions it didn't know how to compile.
This made complex tests compile to empty shells. **Fixed** — undeclared
variables and functions emit errors and exit.

### Link Failures (Fixed)
SDL examples now link against `-lSDL2` / `-lSDL2_net` via per-example
CMakeLists.txt configuration.

### Mismatched Expected Output (Fixed)
`.expected` files corrected for trailing newline mismatches and copy-paste errors.

---

## Tokenizer & Parser Issues (from review.md)

| Issue | Status |
|:------|:-------|
| Tokenizer `*type` swallowing | **Fixed** — `*` and `&` always tokenize as `TOKEN_OPERATOR` |
| `*` deref vs multiplication | **Fixed** — disambiguation moved to parser |
| `(include ...)` silently ignored | **Fixed** — removed from language entirely |
| `(fn name ...)` not supported | **Not needed** — functions are always `(let name (fn ...))` |
| `parse_exp` eats unknown tokens | **Fixed** — now errors with line number |
| `strcmp("fn")` string checks | **Fixed** — dedicated `AST_FN` node type |

---

## Pre-existing Fixes (before cleanup)
- Dead enum values removed (`TOKEN_EXTERN`, `TOKEN_INCLUDE`, `AST_ADDRESS_OF`, `AST_DEREFERENCE`)
- `StructTypeTable` tracked via global variable
- `*type` tokenization fixed (no longer swallowed into single identifier)
- `*` disambiguation moved from tokenizer to parser
- `parse_exp` no longer silently eats unknown tokens
