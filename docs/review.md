# Codebase Review

Audit of the test suite, tokenizer, parser, and codegen — written to establish
a solid foundation for the new codegen. Many items below are now fixed;
see `docs/DONE.md` for completed work and `docs/TODO.md` for remaining items.

---

## 1. Test Suite Audit

### Silent Code Omission (Fixed)
The compiler used to silently ignore expressions it didn't know how to compile.
This made complex tests compile to empty shells. **Now fixed** — undeclared
variables and functions emit errors and exit.

### Link Failures (Fixed)
SDL examples now link against `-lSDL2` / `-lSDL2_net` via per-example
CMakeLists.txt configuration.

### Mismatched Expected Output (Fixed)
`.expected` files corrected for trailing newline mismatches and copy-paste errors.

---

## 2. Tokenizer & Parser — Resolved Issues

| Issue | Status |
|:------|:-------|
| Tokenizer `*type` swallowing | **Fixed** — `*` and `&` always tokenize as `TOKEN_OPERATOR` |
| `*` deref vs multiplication | **Fixed** — disambiguation moved to parser |
| `(include ...)` silently ignored | **Fixed** — removed from language entirely |
| `(fn name ...)` not supported | **Not needed** — functions are always `(let name (fn ...))` |
| `parse_exp` eats unknown tokens | **Fixed** — now errors with line number |
| `strcmp("fn")` string checks | **Fixed** — dedicated `AST_FN` node type |

---

## 3. Codegen Issues (Resolved)

| Issue | Status |
|:------|:-------|
| `+16` stack adjustment magic constant | **Fixed** — `sp_displacement` tracks transient movement |
| `generate_expression_with_adjustment` duplication | **Fixed** — removed; displacement is automatic |
| No lexical scoping | **Fixed** — nested `SymbolTable` tree with `first_child`/`next_sibling` |
| `**` calls C `pow()` with wrong ABI | **Fixed** — custom integer power helper |
| Hardcoded struct/array sizes in arg passing | Open — "assuming 2 fields/4 elements" |
| No type checking | Open — not blocking |
| `ret` return value paths | Needs verification for all control flow |

---

## 4. Recommendations for New Codegen

1. **Dynamic stack offset tracking** in `CodeGen` — no more magic `+16`
2. **Fail-fast error handling** — never silently omit code
3. **ABI conformity** — proper register usage for all calling conventions
4. **IR → CFG → SSA** pipeline for clean optimizations
