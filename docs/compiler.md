# ARM64 Code Generator Documentation

The `clumsy` compiler targets the **ARM64 (AArch64)** architecture, specifically following the macOS ABI and calling conventions. The implementation resides primarily in `src/compiler.c`.

## Code Generation Workflow

The compilation process follows a single-pass, recursive descent pattern:

1.  **Preamble**: Sets up the `.text` section and global `_main` symbol.
2.  **Function Definitions**: Iterates through the AST to generate assembly for all user-defined functions (`let name (fn ...)`).
3.  **Main Entry Point**: Generates the `_main` function, which handles the top-level statements of the program.
4.  **Data Section**: Emits all collected string literals as null-terminated constants under the `.data` section.

## Stack Frame Management

Every function (including `main`) follows a standard ARM64 prologue and epilogue to manage the stack and preserve the execution state:

-   **Prologue**:
    -   Saves the Frame Pointer (`x29`) and Link Register (`x30`) to the stack: `stp x29, x30, [sp, #-16]!`.
    -   Sets the new Frame Pointer: `mov x29, sp`.
    -   Allocates space for local variables by decrementing `sp`.
-   **Epilogue**:
    -   Deallocates local variable space by incrementing `sp`.
    -   Restores `x29` and `x30`: `ldp x29, x30, [sp], #16`.
    -   Returns to the caller: `ret`.

## Register Allocation Strategy

The compiler uses **static register allocation** rather than a dynamic allocator. Registers are assigned fixed semantic roles:

| Register | Role |
| :--- | :--- |
| `x0` | **Accumulator**: Stores the result of the most recent expression and function return values. |
| `x1` | **Auxiliary**: Used for runtime calculations like dynamic array indexing. |
| `x2` | **Left Operand**: Holds the left side of a binary operation (e.g., `+`, `-`, `==`). |
| `x3` | **Right Operand**: Holds the right side of a binary operation. |
| `x4` | **Scratch**: Used for intermediate steps in complex operations (e.g., modulo). |
| `x9` | **Temporary**: Used for moving values between the stack and argument registers. |
| `x0`-`x7` | **Arguments**: Used to pass values to functions according to the ARM64 ABI. |
| `x29` | **Frame Pointer (FP)**: Points to the base of the current stack frame. |
| `x30` | **Link Register (LR)**: Holds the return address for the current function. |

## Expression Evaluation & Recursion

Expressions are evaluated recursively. The result of a sub-expression is always placed in `x0`. For binary operations:
1.  The left operand is evaluated into `x0`, then moved to `x2`.
2.  The right operand is evaluated into `x0`, then moved to `x3`.
3.  The operation (e.g., `add x0, x2, x3`) is performed, placing the final result back in `x0`.

### Register Collision Mitigation
To prevent sub-expressions (like function calls) from overwriting operands currently held in `x2` or `x3`, the compiler:
-   Performs simple lookahead to see if a right-hand operand contains a function call.
-   If a collision is likely, it may reorder the evaluation or use temporary stack storage.

## Limitations
-   **Deep Nesting**: Because it lacks a register spilling mechanism, extremely deep or complex nested expressions may lead to register collisions.
-   **Fixed Stack Sizes**: Current implementations for arrays and structs use hardcoded stack allocations (e.g., 32 bytes for arrays) regardless of the actual declared size.
