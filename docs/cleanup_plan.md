# Cleanup and Code Generator Redo Plan

This document outlines the step-by-step plan to clean up the `clumsy` codebase, reorganize tests, simplify the tokenizer/parser syntax, and prepare for a robust code generator rewrite.

---

## Phase 1: Test Suite Reorganization & Cleanup

### 1. Move Examples/Demos to `examples/` ✅
All demo files moved: `real_tetris`, `sdl_demo`, `sdl_loop`, `sdl_bindings`, `tcp_client`, `tcp_server`, `c_interop_demo`.

### 2. Delete Obsolete/Redundant Tests ✅
- `test_pointer.cpl` (old `*ptr` syntax) — deleted
- `test_loop_basic.expected` (orphan) — deleted, recreated with matching `.cpl`
- `test_power.cpl` — kept (we kept `**` as S-expression)

### 3. Retain & Refactor Core Tests ✅
26 tests retained, all with `.expected` files (except `test_c_interop` compile-smoke). Tests using broken `printf` varargs rewritten to use `print_int`/`print_str` helpers.

---

## Phase 2: Tokenizer & Parser Syntax Revision

### 1. Lexical Simplification ✅
- `*` and `&` always tokenize as `TOKEN_OPERATOR` via maximal-munch operator loop
- No more `*int`/`*char` tokenization as single identifiers

### 2. Enforcing S-Expression Syntax ✅
- **Pointer Types**: `(* int)` instead of `*int`
- **Address-Of**: `(& x)` instead of `&x`
- **Dereference**: `(* ptr)` instead of `*ptr`

### 3. Include Directive — Dropped
Decided `(include ...)` was unnecessary since clumsy compiles to `.s` and links at object level. Removed `AST_INCLUDE_DECL` from the compiler entirely.

---

## Phase 3: Exponentiation Operator (`**`) S-Expression ✅

`(** base exponent)` works via `pow` helper function. Note: the C `pow()` call has an ABI bug (ints passed where doubles expected) — see `docs/TODO.md`.

---

## Phase 4: Compiler Strictness & Error Handling ✅

- Parser `parse_exp` default case: syntax error with line number + exit
- `emit_load_variable_with_adjustment`: undeclared variable → error + exit
- `emit_store_variable`: undeclared variable → error + exit
- `generate_expression` else branch: undeclared function → error + exit

---

## Phase 5: Additional Operator Support ✅

- `=` operator: equality comparison (replaced `==`)
- `not` operator: logical negation `(not expr)` — evaluates to 1 if expr is 0, else 0

---

## Remaining Work

See `docs/TODO.md` for the full list of outstanding items including:
- `+16` stack adjustment elimination
- `generate_expression_with_adjustment` deduplication
- `**` exponentiation ABI fix
- Hardcoded struct/array sizes in arg passing
- Type checking
- Varargs calling convention fix
- Double/float type support
- Negative integer literals
- Null/void types
