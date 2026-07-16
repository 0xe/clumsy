# TODO

Focus: preparing for the new codegen (IR → CFG/DFA → SSA → backends).

## Codegen-blocking

- [ ] **`+16` stack adjustment scattered throughout compiler** — Every `bl` moves `sp` down 16
  (`stp x29, x30, [sp, #-16]!`) and all variable loads inside that sequence manually add 16.
  Fix by tracking `sp` displacement in `CodeGen`.

- [ ] **`generate_expression_with_adjustment` duplication** — Near-identical copy of
  `generate_expression` with a `stack_adjustment` parameter. Eliminate by tracking `sp` offset
  in `CodeGen` instead.

- [ ] **No lexical scoping / `let` inside `begin` broken** — `build_symbol_table` only scans
  top-level statements. Any `let` inside a `begin` is invisible to codegen. Fix requires
  recursing into nested blocks with child symbol tables.

- [ ] **`**` exponentiation ABI** — Compiles `**` into a call to C `pow()`, passing ints in `x0`/`x1`.
  C `pow` expects doubles in `d0`/`d1` and returns in `d0`, causing garbage outputs. Either
  implement an integer power helper or convert operands to FP registers.

- [ ] **Examples SIGSEGV at runtime** — Most examples compile and link but crash with
  segfaults. Root causes: `+16` stack adjustment bugs, wrong calling convention,
  incorrect struct/array field access.

## Cleanup

- [ ] **`SYM_STR` / `SYM_POINTER` overlap** — `str` and `*char` are the same thing but tracked
  as different symbol types. Unify.

- [ ] **Hardcoded struct/array sizes in arg passing** — `"// assuming 2 fields for now"` and
  `"// assuming 4 elements for now"` in the function call argument loop.

- [ ] **String arg handling copy-pasted** — `AST_STRING` in argument lists is handled with the
  same `strdup`/capacity-check block in at least two places.

- [ ] **No dedicated `AST_FN` node** — Done. `fn` forms now use `AST_FN` with named fields.

## Missing language features (for later)

- [ ] **Negative integer literals** — `-1` cannot be written directly; must use `(- 0 1)`.
- [ ] **Null/void types** — No `null` literal or `void` type.
- [ ] **Bitwise operators** — No `&`, `|`, `^`, `~`, `<<`, `>>`.
- [ ] **String operations** — No concatenation, comparison, or length.
- [ ] **Varargs calling convention** — Direct `printf` calls crash; use `print_int`/`print_str` helpers.
- [ ] **No type checking** — Compiler emits code without verifying type compatibility.
- [ ] **Double/float support** — No FP types, registers, or codegen.
