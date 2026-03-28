# TODO

## Hacks to fix

- [ ] **Dead enum values** — `TOKEN_EXTERN`, `TOKEN_INCLUDE` in `TokenType` and `AST_ADDRESS_OF`,
  `AST_DEREFERENCE` in `ASTNodeType` are never produced. Remove or actually use them.

- [ ] **`SYM_STR` / `SYM_POINTER` overlap** — `str` and `*char` are the same thing but tracked
  as different symbol types. Unify.

- [ ] **`*type` swallowed by tokenizer** — `*char` is collapsed into a single `TOKEN_IDENTIFIER`
  with value `"*char"` in the tokenizer. The `*` should be a real token so pointer types can be
  composed generically.

- [ ] **Fragile `*` disambiguation** — `!(idx > 0 && is_digit(source[idx-1]))` only excludes
  `3*x`; `x*y` or `) * y` still hits the wrong branch. Fix with parser-level context instead.

- [ ] **`parse_exp` default case eats unknown tokens** — A stray `)` or any unrecognised token
  is silently consumed and parse recurses. Should error instead.

- [ ] **No dedicated `AST_FN` node** — `fn` forms are stored as plain `AST_LIST` nodes; the
  compiler re-detects them by checking `children[0].value == "fn"`. Give `fn` its own node type
  with named fields (params, return type, body).

- [ ] **`+16` stack adjustment scattered throughout compiler** — Every `bl` moves `sp` down 16
  (`stp x29, x30, [sp, #-16]!`) and all variable loads inside that sequence manually add 16.
  There is no systematic tracking of the current `sp` displacement; the magic constant appears
  in many places.

- [ ] **`generate_expression_with_adjustment` duplication** — Near-identical copy of
  `generate_expression` with a `stack_adjustment` parameter. Eliminate by tracking `sp` offset
  in `CodeGen` instead.

- [ ] **String arg handling copy-pasted** — `AST_STRING` in argument lists is handled with the
  same `strdup`/capacity-check block in at least two places. Extract a helper.

## Incomplete features

- [ ] **`StructTypeTable` always `NULL`** — `main.c` passes `NULL` for `struct_types`;
  `compile_to_arm64` accepts it silently. Struct type resolution is partially wired but
  disconnected end-to-end.

- [ ] **Hardcoded struct/array sizes in arg passing** — `"// assuming 2 fields for now"` and
  `"// assuming 4 elements for now"` in the function call argument loop. Use the symbol table
  to determine actual sizes.

- [ ] **No type checking** — The compiler emits code without verifying that argument types,
  return types, or assignment targets are compatible.

- [ ] **`ret` and return values** — The `ret` form needs to be verified to correctly place the
  return value in `x0` before the function epilogue in all cases.

- [ ] **Negative integer literals** — `-1` cannot be written directly; must be worked around.
  The tokenizer only handles positive integers.

- [ ] **No operator precedence** — All binary operators are prefix s-expressions, which is fine,
  but there is no infix sugar even for common cases.
