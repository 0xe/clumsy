# Clumsy

A Lisp-style language that compiles to ARM64 assembly (macOS, Apple Silicon).

## Quick Start

```bash
# Build the compiler
cmake -B build && cmake --build build

# Compile and run a program
./build/clumsyc hello.cpl > hello.s
as -o hello.o hello.s
cc -o hello hello.o
./hello

# Run all tests
ctest --test-dir build

# Build all examples
cmake --build build --target examples
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
(+ x 1)   (- a b)   (* x y)   (/ n 2)   (% n 2)
(< a b)   (> a b)   (<= a b)  (>= a b)  (= a b)
(not (= a b))
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
(let p *int &x)   // address-of
(* ptr)            // dereference
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

Each `.cpl` file compiles to a `.o`. Link them together:

```clumsy
// board.cpl
(let init-board (fn [...] int
  (begin ...)))

// main.cpl
(extern init-board [...] int)
(init-board)
```

```bash
./build/clumsyc board.cpl > board.s
./build/clumsyc main.cpl > main.s
as -o board.o board.s
as -o main.o main.s
cc -o game board.o main.o
```

## Examples

Example programs live in `examples/`. Build all with:

```bash
cmake --build build --target examples
```

| Example | Description |
|---------|-------------|
| `tetris/` | Full Tetris game using SDL2 |
| `sdl/` | SDL2 window and renderer demo |
| `tcp/` | TCP client/server using SDL2_net |
| `c_interop/` | Demonstrates C function calls |

## Tests

Regression tests live in `tests/` as `.cpl` + `.expected` pairs. Run via CMake:

```bash
cmake --build build && ctest --test-dir build
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
| Bitwise (&, \|, >>, <<, ~) | Done |
| Logical not | Done |
| Negative integer literals | Done |
| Arrays (static, element access, assignment) | Done |
| Structs (field access, nested) | Done |
| Pointers (\*, &, deref) | Done |
| Control flow (if, while) | Done |
| Functions (definition, calls, return) | Done |
| C interop (extern declarations) | Done |
| SDL2 bindings (events, rendering) | Done |
| Multi-file compilation & linking | Done |
| Lexical scoping (nested symbol tables) | Done |
| String literals (interned in .data) | Done |
| Float/double | Not yet |
| Type checking | Not yet |
| IR → SSA pipeline | Planned |
| QBE / LLVM backends | Planned |

## Roadmap

See `docs/TODO.md` for the full plan. Key upcoming work:

1. **IR pipeline** — AST → IR → CFG → SSA → scalar optimizations
2. **Float/double support** — FP registers, ARM64 FP instructions
3. **Type checking pass** — separate validation pass before codegen
4. **QBE/LLVM backends** — portability beyond ARM64/macOS
