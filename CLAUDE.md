# Clumsy

A Lisp-style language that compiles to ARM64 assembly (macOS, Apple Silicon).

## Build

```bash
cmake -B build && cmake --build build
# binary: ./build/clumsyc
```

## Usage

```bash
./build/clumsyc [--debug] <source.cpl>   # emits ARM64 assembly to stdout
./build/clumsyc --debug source.cpl       # also prints AST + symbol table to stderr
```

### Full compile → run pipeline

```bash
./build/clumsyc prog.cpl > prog.s
as -o prog.o prog.s
clang -o prog prog.o -L/opt/homebrew/lib -lSDL2   # add libs as needed
./prog
```

## Language

S-expression syntax. Every statement is a parenthesised form.

### Variables

```clumsy
(let x int 42)
(let msg str "hello")
(set x 99)
```

### Arithmetic & comparison

```clumsy
(+ x 1)   (- a b)   (* x y)   (/ n 2)   (% n 2)
(< a b)   (> a b)   (<= a b)  (>= a b)  (= a b)
(not (= a b))
```

### Control flow

```clumsy
(if condition
  then-expr
  else-expr)

(while condition
  (begin
    stmt1
    stmt2
  ))
```

### Functions

```clumsy
(let add (fn [(a int) (b int)] int
  (+ a b)))

(add 3 4)
```

### C interop

Declare external C functions, then call them normally. macOS ABI: `_`-prefix is added automatically.

```clumsy
(extern puts [*char] int)
(extern SDL_Init [int] int)

(puts "hello")
(SDL_Init 0x20)
```

### Pointers

```clumsy
(let p *int &x)   // address-of
*p                // dereference
```

### Hex literals

```clumsy
(let flags int 0x2FFF0000)
```

## Architecture

```
src/tokenizer.c   Tokenizer → TokenArray
src/parser.c      TokenArray → ASTNode tree
src/compiler.c    ASTNode tree → ARM64 assembly string (stdout)
src/types.h       Shared types: Token, ASTNode, Symbol, SymbolTable
src/main.c        CLI driver
```

### Key data structures

- **`ASTNode`** — tagged union: `AST_INT`, `AST_STRING`, `AST_IDENTIFIER`, `AST_LIST` (general s-expr), `AST_EXTERN_DECL`, `AST_ARRAY`, etc.
- **`SymbolTable`** — flat array of `Symbol` with a `parent` pointer for scope chaining. `find_symbol_recursive` walks the chain.
- **`Symbol.type`** — `SYM_INT`, `SYM_STR`, `SYM_FUNCTION`, `SYM_C_FUNCTION`, `SYM_POINTER`, etc. `SYM_FUNCTION`/`SYM_C_FUNCTION` are **excluded from stack slot allocation**.
- **`CodeGen`** — holds the growing output string and a string literal pool (`strings[]`); pool is flushed to `.data` section at the end of compilation.

### Codegen notes (ARM64 / macOS)

- Entry point is `_main`; frame setup is `stp x29, x30, [sp, #-16]!` / `mov x29, sp` / `sub sp, sp, #N`.
- Each local variable occupies 8 bytes on the stack (arrays 32, structs 16). Stack size is computed by `build_symbol_table` before codegen.
- Before each `bl` (function call), another `stp x29, x30, [sp, #-16]!` saves the link register; after the call, `ldp x29, x30, [sp], #16` restores it. Variable loads inside a call sequence must add **+16** to their base offset to account for this.
- String literals: `adrp x9, str_N@PAGE` / `add x9, x9, str_N@PAGEOFF`. Strings are interned in `codegen->strings[]` and emitted in the `.data` section.
- C function calls on macOS prepend `_` to the symbol name.
- Argument registers: **x0–x7** (8 args max per ARM64 ABI).

### Newline handling

`parse_exp` skips newlines/comments. `parse_sexp`'s inner loops **also** skip newlines before checking for the closing `)` — this is critical. If you add new loop constructs to the parser, follow the same pattern:

```c
while (true) {
    while (match_token(parser, TOKEN_NEWLINE) || match_token(parser, TOKEN_COMMENT))
        next_token(parser);
    if (match_token(parser, TOKEN_RPAREN) || match_token(parser, TOKEN_EOF)) break;
    // parse child
}
```

## Tests

Regression tests live in `tests/` as `.cpl` + `.expected` pairs. Run via CMake:

```bash
cmake --build build && ctest --test-dir build
```

Tests with no `.expected` file are compile-smoke tests only.

## SDL2 on macOS

Link with `-L/opt/homebrew/lib -lSDL2`. Call `SDL_PumpEvents` each frame — without it the macOS compositor won't display the window.
