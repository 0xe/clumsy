# Cleanup and Code Generator Redo Plan

This document outlines the step-by-step plan to clean up the `clumsy` codebase, reorganize tests, simplify the tokenizer/parser syntax, and prepare for a robust code generator rewrite.

---

## Phase 1: Test Suite Reorganization & Cleanup

We will consolidate the test suite to focus on **quality over quantity**, moving example programs to an `examples/` directory and removing obsolete tests.

### 1. Move Examples/Demos to `examples/`
The following files are demo programs or bindings rather than unit tests. They will be moved to `/examples` at the root of the project:
*   `tests/real_tetris.cpl` & `tests/real_tetris.s` &rarr; `examples/real_tetris.cpl`
*   `tests/sdl_demo.cpl` &rarr; `examples/sdl_demo.cpl`
*   `tests/sdl_loop.cpl` &rarr; `examples/sdl_loop.cpl`
*   `tests/sdl_bindings.cpl` &rarr; `examples/sdl_bindings.cpl`
*   `tests/tcp_client.cpl` &rarr; `examples/tcp_client.cpl`
*   `tests/tcp_server.cpl` &rarr; `examples/tcp_server.cpl`
*   `tests/c_interop_demo.cpl` &rarr; `examples/c_interop_demo.cpl`

### 2. Delete Obsolete/Redundant Tests
*   `tests/test_power.cpl` & `tests/test_power.expected` (Since we are removing `**` exponentiation).
*   `tests/test_pointer.cpl` (Since we are deprecating the `*ptr` syntax in favor of `(* ptr)`).
*   `tests/test_loop_basic.expected` (Dangling expected file with no corresponding `.cpl` test).

### 3. Retain & Refactor Core Tests
We will keep a subset of ~15 high-quality tests covering the core language features:
1.  `test_int_literals.cpl` (Integers and hex constants)
2.  `test_if.cpl` (Conditionals)
3.  `test_loop_basic.cpl` (We will create a clean basic loop test to replace the missing one)
4.  `test_factorial_iterative.cpl` (Iterative while loop, set statement, multiplication)
5.  `test_factorial_recursive.cpl` (Recursion, function calls, conditional returns)
6.  `test_fn_add.cpl` (Basic function definition and call)
7.  `test_funcall_simple.cpl` (Function calls as sub-expressions)
8.  `test_funcall_assignment.cpl` (Function call assignments)
9.  `test_more_functions.cpl` (Multiple function interactions)
10. `test_pointer2.cpl` (Pointers using clean `(* ptr)` and `(& x)` syntax)
11. `test_deref_expr.cpl` (Dereferences inside expressions)
12. `test_struct.cpl` (Struct definition, instantiation, and field access)
13. `test_array_basic.cpl` (Array definition and indexing)
14. `test_c_interop_puts_string.cpl` (External puts call with string)
15. `test_c_interop_puts_null.cpl` (External puts call with null pointer)
16. `test_bug_statement_processing.cpl` (Puts execution bug check)

---

## Phase 2: Tokenizer & Parser Syntax Revision

We will simplify pointer syntax and implement textual file inclusion:

1.  **Lexical Simplification**:
    *   Remove naive asterisk/ampersand disambiguation from the tokenizer.
    *   `*` and `&` will always be tokenized as standalone `TOKEN_OPERATOR` tokens (or dedicated tokens).
    *   Remove tokenization of `*int`, `*char`, etc. as single identifiers.
2.  **Enforcing S-Expression Syntax**:
    *   **Pointer Types**: Parsed as `(* int)` instead of `*int`.
    *   **Address-Of**: Enforced as `(& x)` instead of `&x`.
    *   **Dereference**: Enforced as `(* ptr)` instead of `*ptr`.
3.  **Include Directive (Copy-Paste Text Inclusion)**:
    *   Implement textual inclusion inside the parser or tokenizer. When `(include "filename")` is parsed, we read the target file, tokenize it, and insert its token stream directly into the parent token stream. This acts like `#include` in C.

---

## Phase 3: Exponentiation Operator (`**`) S-Expression

We will keep the `**` exponentiation operator and ensure it works correctly as an S-expression:
*   The tokenizer will tokenize `**` as a single `TOKEN_OPERATOR("**")` token (via maximal-munch).
*   The parser will parse `(** base exponent)` as a standard s-expression list.
*   The compiler already has correct codegen for `**` via the `pow` helper function — no compiler changes needed.
*   Ensure `test_power.cpl` uses `(** base exponent)` syntax and passes.

---

## Phase 4: Compiler Strictness & Error Handling

We will change the compiler from silent mode to fail-fast:
*   If the parser encounters an unrecognized token in `parse_exp`, print a syntax error and exit with a non-zero code.
*   If the symbol builder or code generator encounters an undeclared variable or function, print an error and exit with a non-zero code.
*   If code generation fails or encounters unsupported constructs (like dead code/unhandled AST nodes), abort with exit code `1`.
