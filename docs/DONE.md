# Completed Work

## Cleanup & Syntax Revision (Phases 1–5)

### Test Suite Reorganization
- Demos moved to `examples/`: real_tetris, sdl_demo, sdl_loop, sdl_bindings, tcp_client, tcp_server, c_interop_demo
- Deleted obsolete tests; recreated `test_loop_basic.cpl`
- 26 tests retained, all with `.expected` files (except `test_c_interop` compile-smoke)
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

### AST_FN Node Type
- Dedicated `AST_FN` node with named fields: `fn.params`, `fn.param_count`, `fn.return_type`, `fn.body`
- Removed fragile `strcmp("fn")` string checks from parser, compiler, and symbol table construction

### Examples
- All 7 examples compile and link with proper SDL2/SDL2_net flags
- Variables moved to top level (workaround for `build_symbol_table` not recursing into `begin`)

### Build System
- `print_helpers` as CMake STATIC library (not standalone `.o`)
- Per-example SDL link libs via CMakeLists.txt pattern matching
- `example_runner.cmake` parses space-separated `LINK_LIBS`

### Documentation
- `docs/review.md`: audit of test suite, tokenizer, parser, and codegen defects
- `docs/TODO.md`: prioritized list of remaining work

---

## Pre-existing Fixes (before cleanup)
- Dead enum values removed (`TOKEN_EXTERN`, `TOKEN_INCLUDE`, `AST_ADDRESS_OF`, `AST_DEREFERENCE`)
- `StructTypeTable` tracked via global variable (still needs clean threading)
- `*type` tokenization fixed (no longer swallowed into single identifier)
- `*` disambiguation moved from tokenizer to parser
- `parse_exp` no longer silently eats unknown tokens
