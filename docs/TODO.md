# TODO

Focus: preparing for the new codegen (IR → CFG/DFA → SSA → backends).

## Codegen-blocking

- [ ] **Examples SIGSEGV at runtime** — Some examples still crash with segfaults.
  Remaining root causes under investigation.

## Cleanup

- [ ] **`SYM_STR` / `SYM_POINTER` overlap** — `str` and `*char` are the same thing but tracked
  as different symbol types. Unify.

- [ ] **Hardcoded struct/array sizes in arg passing** — `"// assuming 2 fields for now"` and
  `"// assuming 4 elements for now"` in the function call argument loop.

- [ ] **String arg handling copy-pasted** — `AST_STRING` in argument lists is handled with the
  same `strdup`/capacity-check block in at least two places.

- [x] **No dedicated `AST_FN` node** — Done. `fn` forms now use `AST_FN` with named fields.

## Missing language features (for later)

- [ ] **Negative integer literals** — `-1` cannot be written directly; must use `(- 0 1)`.
- [ ] **Null/void types** — No `null` literal or `void` type.
- [ ] **Bitwise operators** — No `&`, `|`, `^`, `~`, `<<`, `>>`.
- [ ] **String operations** — No concatenation, comparison, or length.
- [ ] **Varargs calling convention** — Direct `printf` calls crash; use `print_int`/`print_str` helpers.
- [ ] **No type checking** — Compiler emits code without verifying type compatibility.
- [ ] **Double/float support** — No FP types, registers, or codegen.
