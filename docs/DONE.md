# Completed Work

## Cleanup & Syntax Revision (Phases 1–5)

### Test Suite Reorganization
- Demos moved to `examples/`: real_tetris, sdl_demo, sdl_loop, sdl_bindings, tcp_client, tcp_server, c_interop_demo
- Deleted obsolete tests; recreated `test_loop_basic.cpl`
- 31 tests retained, all with `.expected` files (except `test_c_interop` compile-smoke)
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
