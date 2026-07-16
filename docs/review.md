# Codebase Review: Compiler & Test Suite Audit

This document evaluates the current test suite, identifies defects in the tokenizer, parser, and code generator, and analyzes the bugs listed in `TODO.md`. The goal is to establish a solid foundation for redoing the code generator.

---

## 1. Test Suite Audit ("Why You Shouldn't Trust the Tests")

A detailed examination of the tests reveals that **the test suite is highly untrustworthy**. While CMake reports that 63% of the tests pass, the passing status is often a false positive due to silent compiler dropouts, missing dependencies, and incorrect expectations.

### A. The "Silent Code Omission" False-Positives (Critical)
The compiler possesses a critical design flaw: **it silently ignores expressions and statements it doesn't know how to compile (like calls to undeclared functions) instead of throwing a compilation error.** 

Because of this, several complex tests compile to empty shells that do nothing but allocate local variables and exit immediately:
*   **`real_tetris.cpl` (Dummy Test - Passed)**: This test defines its functions using `(fn init_board [] ...)` directly at the top level. The compiler only recognizes functions defined with a `let` binding (e.g., `(let init_board (fn [] ...))`). Top-level `fn` declarations are silently parsed as generic lists and then **completely dropped** during code generation. The resulting executable is a dummy `_main` function that assigns a few constants and returns `0`. It has no game loop, no rendering, and no C calls.
*   **`sdl_demo.cpl` (Dummy Test - Passed)**: The compiler silently ignores the `(include "sdl_bindings.cpl")` statement (see below). As a result, all SDL functions (like `SDL_Init`) are undeclared. Because the compiler silently drops calls to undeclared functions, the entire SDL logic is omitted from the generated assembly. The test "passes" simply because it prints a few strings and exits.
*   **`test_pointer.cpl`, `test_pointer2.cpl`, `test_deref_expr.cpl` (Dummy Tests - Passed)**: These tests contain pointer dereferences (`*ptr` or `(* ptr)`) and address-of operations (`&x`). The code generator **contains no implementation for address-of or dereference**, silently emitting nothing (returning `0` on evaluation). Because these tests lack `.expected` files, the test runner marked them as passing because they successfully compiled and exited.

### B. Link Failures due to Test Runner Configuration
*   **`sdl_loop.cpl`, `tcp_client.cpl`, `tcp_server.cpl` (Failed to Link)**: These tests fail at the link step because they use functions from SDL2 and SDL2_net. The CMake `test_runner.cmake` compiles them using `clang -o test.x test.s print_helpers.o` but fails to link them against the external libraries (e.g., `-lSDL2` or `-lSDL2_net`).

### C. Mismatched Expected Output Files
*   **`test_c_interop_puts_null.expected` (Copy-Paste Error)**: The expected file contains 3 lines of `(null)`, but the source code `test_c_interop_puts_null.cpl` only calls `(puts null)` once. This appears to be a copy-paste error from `test_c_interop_multi_args.expected`.
*   **Trailing Newline Mismatches**: C's standard `puts` automatically adds a trailing newline. The `.expected` files for `test_c_interop_multi_args`, `test_c_interop_puts_string`, and `test_bug_statement_processing` do not have trailing newlines on their last lines, causing `ctest` to fail on output mismatch even though the code compiles and runs correctly.

### D. Missing Test Files
*   **`test_loop_basic.expected`** exists in `tests/`, but the corresponding test file `test_loop_basic.cpl` is missing from the directory.

---

## 2. Tokenizer & Parser Correctness Analysis

Although the user-facing compiler frontend (tokenizer and parser) is supposed to be working, it contains several critical defects that make it fragile and incompatible with standard Lisp/Scheme paradigms.

### A. The Tokenizer's Pointer Hijacking (Critical)
The tokenizer attempts to disambiguate pointer types (like `*int`), dereferences (`*ptr`), and multiplication (`*`) at the lexical level, which is fundamentally broken:
1.  **Swallowing Pointer Types**: Any token starting with `*` and followed by a letter (e.g., `*char`, `*int`) is tokenized as a single `TOKEN_IDENTIFIER` with the value `"*char"`. This prevents generic composition of pointer types (like pointer-to-pointer `**char` or pointer-to-struct `*Point`).
2.  **Hijacking Multiplication**: The tokenizer classifies `*` as a `TOKEN_DEREFERENCE` unless it is immediately preceded by a digit (like `3*x`). In standard prefix notation `(* result i)`, the `*` is preceded by `(`, which is not a digit. Thus, the multiplication operator is tokenized as a dereference.
3.  **Parser Fragmentation**: In `(set result (* result i))`, the tokenizer yields `*` as a dereference. The parser consumes it and parses `(* result)` as a dereference list, leaving `i` as a separate sibling expression. The list is parsed as `(set result (* result) i)` rather than a 3-element list. Because `(* result)` has only 2 elements, the compiler's multiplication handler (which requires $\ge 3$ elements) generates no code, leaving `x0` with the loop comparison result (`1`). This is the root cause of `test_factorial_iterative` printing `1` instead of `120`.

### B. Ignoring File Inclusions (`include`)
The parser correctly parses `(include "filename.cpl")` into an `AST_INCLUDE_DECL` node. However, **neither the symbol table builder nor the code generator implements file inclusion.** It is silently skipped, preventing modular bindings (like `sdl_bindings.cpl`) from loading.

### C. Missing Syntax Support for `(fn name ...)`
The parser only recognizes anonymous functions when they are bound via `(let name (fn [...] type body))`. The standard function syntax `(fn name [...] type body)` is parsed as a generic list and silently discarded by the compiler.

### D. Silent Consumption of Unknown Tokens
In `parse_exp` (`src/parser.c`), the default case of the token switch silently consumes unrecognized tokens (such as stray parentheses) and recurses, hiding syntax errors from the user.

---

## 3. TODO.md Defects & Severity Analysis

Here is a breakdown of the bugs and incomplete features listed in `TODO.md` with their severity and impact on the code generator rewrite:

| Bug/Feature | Description & Root Cause | Severity | Impact on Code Generator Redo |
| :--- | :--- | :--- | :--- |
| **Fragile `*` Disambiguation** | Tokenizer uses naive lookbehind to identify multiplication; classifies prefix `*` as dereference. | **Critical** | **High**. Prevents all prefix multiplication `(* a b)` from compiling if there is a space before `*`. |
| **Swallowed `*type`** | Tokenizer collapses `*char` into a single identifier token. | **High** | **High**. Prevents composing pointer types genericially (`**char`). |
| **`+16` stack adjustment scattered** | Every `bl` call shifts `sp` by 16. Variables inside call sequences add a magic `+16` to their base offset. | **High** | **High**. Very fragile. Stack offsets must be tracked dynamically via a displacement offset in `CodeGen`. |
| **Hardcoded struct/array sizes** | Struct and array offsets assume 2 fields and 4 elements during argument passing. | **High** | **High**. Will corrupt the stack if arrays of different sizes or structs of different shapes are passed. |
| **`parse_exp` eats tokens** | Unrecognized tokens are silently skipped in parser. | **High** | **Medium**. Hides syntax errors, making compiler debugging extremely difficult. |
| **No Type Checking** | The compiler generates assembly without verifying type compatibility. | **High** | **Medium**. Essential for language safety, though codegen can technically output untyped assembly. |
| **`**` Exponentiation ABI** | Compiles `**` into a call to C `pow()`. Passes ints in `x0`/`x1` and reads result from `x0`. | **Critical** | **High**. Calls standard C `pow` which expects doubles in `d0`/`d1` and returns in `d0`, causing garbage outputs. |
| **StructTypeTable always `NULL`** | `main.c` passes `NULL` to `compile_to_arm64`. | **Medium** | **Low**. Bypassed via a global variable hack in `compiler.c`. Should be passed cleanly. |
| **~~No dedicated `AST_FN` node~~** | ~~`fn` is parsed as `AST_LIST` instead of a dedicated AST node with fields.~~ **Resolved**: `AST_FN` node with named fields (`fn.params`, `fn.body`, etc.). | **Medium** | **Medium**. |
| **`ret` return value** | Verification needed to ensure return values are in `x0` in all control-flow paths. | **Medium** | **Medium**. Essential for robust function return generation. |
| **Negative integer literals** | Tokenizer lacks negative sign handling for integer constants. | **Medium** | **Medium**. Forces users to write `(- 0 1)` instead of `-1`. |
| **Dead enum values** | Unused tokens like `TOKEN_EXTERN` in `types.h`. | **Low** | **Low**. Code cleanup item. |

---

## 4. Recommendations for the Code Generator Rewrite

To build a reliable and robust code generator, the following architectural improvements must be implemented:

1.  **Lexical Cleanliness**:
    *   Stop attempting type disambiguation in the tokenizer. Treat `*` and `&` as distinct, individual operator tokens.
    *   Let the parser compose pointer types (e.g., parsing a list of `*` followed by a type identifier into an `AST_POINTER_TYPE` node).
2.  **Fail-Fast Error Handling**:
    *   Throw compilation errors and abort compilation when encountering undeclared variables or functions. **Never silently omit code.**
    *   Abort parsing and print descriptive errors when unrecognized tokens are encountered.
3.  **Proper Dynamic Stack Offset Tracking**:
    *   Introduce a `stack_displacement` tracker in `CodeGen` that increments whenever the stack pointer is modified (e.g., during function call frame setups) and decrements when restored.
    *   Calculate variable stack offsets by adding `base_offset + codegen->stack_displacement` rather than scattering magic `+16` offsets.
4.  **ABI Conformity**:
    *   Follow the ARM64 calling convention strictly. For `pow` (`**`), either implement an integer power helper function or convert operands to floating-point registers (`d0`/`d1`) before calling `_pow`.
    *   Lookup array and struct sizes from the symbol table rather than hardcoding assumptions like "4 elements" or "2 fields".
