# Clumsy

A Lisp-style language that compiles to ARM64 assembly (macOS, Apple Silicon).

## Quick Start

```bash
# Build the compiler (default backend: qbe)
cmake -B build-qbe && cmake --build build-qbe

# Compile and run a program
./build-qbe/clumsyc hello.cpl > hello.s
as -o hello.o hello.s
cc -o hello hello.o
./hello

# Run all tests
ctest --test-dir build-qbe

# Build all examples
cmake --build build-qbe --target examples
```

Each backend is tested in its own build dir, configured with `-DTEST_BACKEND`:

```bash
cmake -B build-qbe        -DTEST_BACKEND=qbe       # default
cmake -B build-llvm       -DTEST_BACKEND=llvm
cmake -B build-cranelift  -DTEST_BACKEND=cranelift
cmake -B build-legacy     -DTEST_BACKEND=aarch64-legacy
```

## Language

S-expression syntax.

### Variables

```clumsy
(let x int 42)
(let msg str "hello")
(set x 99)
```

### Arithmetic & Comparison

```clumsy
(+ x 1)   (- a b)   (* x y)   (/ n 2)   (% n 2)   (** base exp)
(< a b)   (> a b)   (<= a b)  (>= a b)  (= a b)
(not (= a b))
```

### Logical Operators

```clumsy
(! expr)          // prefix logical not
(&& a b)          // logical AND (short-circuit)
(|| a b)          // logical OR (short-circuit)
```

### Bitwise Operations

```clumsy
(& flags 0xFF)    // AND
(| flags 0x10)    // OR
(>> x 3)          // right shift
(<< x 2)          // left shift
(~ flags)          // bitwise NOT
```

### Control Flow

```clumsy
(if condition
  then-expr
  else-expr)

(while condition
  (begin
    stmt1
    (if condition (break))       // exit loop
    (if other (continue))       // skip to next iteration
    stmt2))
```

### Functions

```clumsy
(let add (fn [(a int) (b int)] int
  (+ a b)))

(add 3 4)
(ret value)   // early return from any position
```

### Pointers

```clumsy
(let p *int (& x))   // address-of
(* p)                 // dereference
```

### Arrays

```clumsy
(let arr int[5] [10 20 30 40 50])
arr[2]             // read element
(set arr[1] 99)    // write element
```

### Structs

```clumsy
(let point struct
  #((x int 0)
    (y int 0)))

(. point x)        // field access
(set (. point y) 10)
```

### C Interop

Declare external C functions, then call them normally.

```clumsy
(extern puts [*char] int)
(extern SDL_Init [int] int)

(puts "hello")
(SDL_Init 0x20)
```

### Multi-File Programs

Each `.cpl` file compiles to a `.o`. Use `pub` to export, `extern` to import:

```clumsy
// config.cpl — library file, no main
(pub COLS int 10)
(pub ROWS int 20)

// render.cpl — library file
(extern COLS int)
(extern ROWS int)
(pub render (fn [...] void ...))

// main.cpl — entry point
(extern COLS int)
(extern render [...] void)
(pub main (fn [] int
  (render ...)
  (ret 0)))
```

```bash
./build-qbe/clumsyc config.cpl > config.s
./build-qbe/clumsyc render.cpl > render.s
./build-qbe/clumsyc main.cpl > main.s
as -o config.o config.s
as -o render.o render.s
as -o main.o main.s
cc -o game config.o render.o main.o
```

The example runner handles this automatically for files in `examples/`:

```bash
cmake --build build-qbe --target build-tetris
```

## Examples

Example programs live in `examples/`. Build all with:

```bash
cmake --build build-qbe --target examples
```

| Example | Description |
|---------|-------------|
| `tetris/` | Full Tetris game (4 files: config, tetrominos, render, main) |
| `sdl/` | SDL2 window and renderer demo |
| `c_interop/` | Demonstrates C function calls |
| `tcp_client/` | TCP client using SDL2_net |
| `tcp_server/` | TCP server using SDL2_net |

## Tests

Regression tests live in `tests/` as `.cpl` + `.expected` pairs. Run via CMake:

```bash
cmake --build build-qbe && ctest --test-dir build-qbe
```

## Build Targets

| Target | Description |
|--------|-------------|
| `clumsyc` | The compiler binary |
| `examples` | Build all example programs |
| `test-*` | Individual test (auto-discovered from `tests/*.cpl`) |

## Status

| Feature | Status |
|---------|--------|
| Variables (int, str, char) | Done |
| Arithmetic (+, -, \*, /, %, \*\*) | Done |
| Comparisons (=, <, >, <=, >=) | Done |
| Logical operators (!, &&, \|\|, not) | Done |
| Bitwise (&, \|, >>, <<, ~) | Done |
| Loop control (break, continue) | Done |
| Negative integer literals | Done |
| Arrays (static, element access, assignment) | Done |
| Structs (field access, nested) | Done |
| Pointers (\*, &, deref) | Done |
| Control flow (if, while) | Done |
| Functions (definition, calls, return) | Done |
| C interop (extern declarations) | Done |
| SDL2 bindings (events, rendering) | Done |
| Multi-file compilation & linking | Done |
| `pub` / `extern` symbol visibility | Done |
| Global variables (.data section) | Done |
| Lexical scoping (nested symbol tables) | Done |
| String literals (interned in .data) | Done |
| Variadic printf (all 4 backends, Apple AArch64) | Done |
| IR → CFG → SSA pipeline (dominance, phi placement) | Done |
| QBE / LLVM / Cranelift backends | Done |
| Scalar optimizations (fold, DCE, copy-prop) | Not yet |
| Float/double | Not yet |
| Type checking | Not yet |

## Roadmap

See `docs/TODO.md` for the full plan. Key upcoming work:

1. **Scalar optimizations on SSA** — constant folding, DCE, copy propagation, strength reduction
2. **Float/double support** — FP registers (d0–d31), ARM64 FP instructions
3. **Type checking pass** — separate validation pass before codegen

Note: the language already compiles to ARM64 (aarch64-legacy), QBE IL, LLVM IR,
and Cranelift CLIF on macOS; see `docs/TODO.md` for per-phase details.
