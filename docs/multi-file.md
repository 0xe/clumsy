# Multi-File Compilation

Design decisions for splitting Clumsy programs across multiple files.

## File types

**Library file** — provides definitions (functions, globals) to other files. No top-level
executable code. Does not generate `_main`.

**Program file** — the entry point. Defines `(pub main (fn [] int ...))`. Only one `main`
per program — the linker catches duplicates, so the compiler doesn't need to check.

Top-level statements are **declarations only**: `let`, `pub`, `extern`, `struct`, `fn`
definitions. No executable code (`print`, arithmetic, etc.) at file scope.

```clumsy
// ALLOWED at top level
(let COLS int 10)
(pub ROWS int 20)
(extern printf [(* char) int] int)
(struct Vec2 [x int y int])
(let add (fn [(a int) (b int)] int (+ a b)))

// NOT ALLOWED — compile error
(print "hello")
(+ 1 2)
```

## Public symbols with `pub`

`pub` is a keyword parallel to `let`. It declares a symbol with external linkage:

```clumsy
// file-private (no .globl)
(let COLS int 10)
(let render (fn [...] void ...))

// exported (.globl)
(pub ROWS int 20)
(pub main (fn [] int ...))
(pub render (fn [...] void ...))
```

Inside functions, the same distinction applies:

```clumsy
(let main (fn [] int
  (let x int 10)            // local — stack
  (pub counter int 0)       // global — .data
  (set counter 1)           // mutates global
  (set x 2)                 // mutates local
  (ret 0)))
```

Without `pub`, the symbol is file-private — no `.globl`, no linker visibility.

This is a **breaking change**: functions are no longer global by default. All existing
multi-file programs that call functions across files need `pub` on the exporting side
and `extern` on the calling side.

## Global variables

Top-level `let`/`pub` emit to the `.data` section (not the stack). Any file can
reference them with `extern`.

- `(let COLS int 10)` — file-private global
- `(pub COLS int 10)` — exported global
- `(extern COLS int)` — reference from another file (linker resolves)

`set` works on globals the same as locals — the codegen uses `adrp`/`add` addressing
instead of stack offsets.

Array globals: `(pub board int[200] [0])` emits `.zero 1600` in `.data`.
String globals: `(pub msg str "hello")` emits `.asciz "hello"` in `.data`.

## Extern declarations

`(extern name type)` references a symbol from another file. Works for both functions
and variables:

```clumsy
(extern printf [(* char) int] int)   // C function
(extern COLS int)                     // Clumsy global
(extern render [(* void)] void)       // Clumsy function
```

The compiler uses `extern` to determine the symbol's type and signature. The linker
resolves the actual address.

## Example project layout

```
project/
  config.cpl       (pub ROWS, pub COLS, pub EMPTY)
  tetrominos.cpl   (pub SHAPE — pub fn for shape access)
  render.cpl       (pub render — pub fn using SDL)
  main.cpl         (pub main — program entry point)
```

```clumsy
// config.cpl
(pub ROWS int 20)
(pub COLS int 10)
(pub EMPTY int -1)

// tetrominos.cpl
(pub SHAPE int[224] [...])
(pub blocked (fn [...] int ...))

// render.cpl
(extern SHAPE int[224])
(extern ROWS int)
(extern COLS int)
(extern EMPTY int)
(pub render (fn [...] void ...))

// main.cpl
(extern ROWS int)
(extern COLS int)
(extern render [(* void) (* int) int] void)
(pub main (fn [] int
  (let board int[200] [0])
  (render ...)
  (ret 0)))
```

Build: `clumsyc config.cpl tetrominos.cpl render.cpl main.cpl -o game`

## Migration checklist

- [ ] Add `pub` to all functions/variables exported from library files
- [ ] Add `extern` declarations in files that reference cross-file symbols
- [ ] Wrap top-level executable code in `(pub main (fn [] int ...))`
- [ ] Update all 34 tests to define `main` explicitly
- [ ] Update example programs to use `main`
- [ ] Update `CLAUDE.md` language reference

## Compiler changes required

1. **`pub` keyword** — parser treats `pub` like `let` but sets `is_public` on the symbol
2. **Global `.data` emission** — top-level `let`/`pub` emit to `.data` instead of stack
3. **`extern` for variables** — `(extern name type)` without parameter list
4. **`is_global` flag on Symbol** — `emit_load_variable`/`emit_store_variable` check it
5. **Compiler doesn't check for `main`** — linker catches duplicates; skip `_main` generation
   if no `pub main (fn ...)` is found (library file behavior)
6. **Error on top-level executable code** — catch stray statements outside declarations
7. **Functions are local by default** — only `pub` emits `.globl`
8. **Inside functions, `pub` creates globals** — `(pub x int 10)` inside a fn emits to `.data`
