# Clumsy Codebase Exploration

## Token Types (src/tokenizer.c, src/types.h)

| Token Type | Meaning |
|---|---|
| `TOKEN_LPAREN` | `(` |
| `TOKEN_RPAREN` | `)` |
| `TOKEN_LBRACKET` | `[` |
| `TOKEN_RBRACKET` | `]` |
| `TOKEN_QUOTE` | `'` (Scheme-style quote) |
| `TOKEN_INT` | Decimal (`42`) and hex (`0xFF`) integer literals, including negative (`-1`) |
| `TOKEN_STRING` | Double-quoted strings with escape support (`"hello\n"`) |
| `TOKEN_CHAR` | Scheme-style character literals (`#\c`) |
| `TOKEN_IDENTIFIER` | Any alphanumeric name starting with alpha or `_` |
| `TOKEN_KEYWORD` | Reserved words: `int`, `str`, `let`, `bool`, `char`, `struct`, `set`, `fn`, `ret`, `if`, `else`, `while`, `extern`, `include`, `null` |
| `TOKEN_OPERATOR` | `+ - * / % ** ! ~ . < > | & = <= >= \|\| && >> << #` (maximal munch, longest match first) |
| `TOKEN_COMMENT` | `//` line comments |
| `TOKEN_NEWLINE` | Newlines |
| `TOKEN_EOF` | End of file |

**Notes:**
- Keywords vs identifiers distinguished by `is_keyword()` post-hoc.
- Operators matched greedily (2-char before 1-char).
- Negative number literals tokenized directly as `TOKEN_INT` when `-` immediately followed by digit.
- `*` and `&` are `TOKEN_OPERATOR` — disambiguated by arity in parser (unary prefix = address-of/dereference, binary = bitwise AND/multiply).
- `'` creates `TOKEN_QUOTE` with value `"quote"`.
- `#\c` creates `TOKEN_CHAR`.
- `#(` creates `TOKEN_OPERATOR` value `#` + `TOKEN_LPAREN`.

---

## AST Node Types (src/types.h)

```c
typedef enum {
    AST_INT,          // Integer literal
    AST_STRING,       // String literal
    AST_CHAR,         // Character literal
    AST_IDENTIFIER,   // Variable name, operator name, or keyword
    AST_LIST,         // General s-expression: (op child1 child2 ...)
    AST_ARRAY,        // Array literal: [1 2 3 4]
    AST_EXTERN_DECL,  // Extern C function declaration
    AST_ADDRESS_OF,   // Defined but unused; & parsed as AST_LIST with "&"
    AST_DEREFERENCE,  // Defined but unused; * parsed as AST_LIST with "*"
    AST_FN            // Function definition
} ASTNodeType;
```

### ASTNode union fields (`data`)

| Node Type | Fields | Description |
|---|---|---|
| `AST_INT` | `int int_value` | Integer literal value |
| `AST_STRING` | `char *string_value` | String literal (including quotes) |
| `AST_CHAR` | `char char_value` | Single character |
| `AST_IDENTIFIER` | `char *string_value` | Name string |
| `AST_LIST` | `ASTNode **children`, `size_t count`, `size_t capacity` | Dynamic array of child nodes |
| `AST_ARRAY` | Same as `AST_LIST` | Array literal `[elem1 elem2 ...]` |
| `AST_EXTERN_DECL` | Same as `AST_LIST` | `[name_node, params_list, return_type_node]` |
| `AST_FN` | `ASTNode **params`, `size_t param_count`, `ASTNode *return_type`, `ASTNode *body` | Separate fields |

Every `ASTNode` has a `struct SymbolTable *scope` field set by `build_symbol_table` on `begin`, `if`, and `while` body nodes.

### How the parser produces AST nodes

- `(let x int 42)` → `AST_LIST [IDENTIFIER:"let", IDENTIFIER:"x", IDENTIFIER:"int", INT:42]`
- `(if cond then else)` → `AST_LIST [IDENTIFIER:"if", cond, then, else]`
- `(fn [(a int) (b int)] int (+ a b))` → `AST_FN` node
- `arr[0]` → `AST_LIST [IDENTIFIER:"[]", IDENTIFIER:"arr", INT:0]`
- `p.x` → `AST_LIST [IDENTIFIER:".", IDENTIFIER:"p", IDENTIFIER:"x"]`
- `#((x 10) (y 20))` → `AST_LIST [IDENTIFIER:"#", sexp_of_fields]`
- `[1 2 3]` → `AST_ARRAY [INT:1, INT:2, INT:3]`

---

## Symbol Types (src/types.h)

| SymbolType | Stack allocation |
|---|---|
| `SYM_INT` | 8 bytes |
| `SYM_STR` | 8 bytes |
| `SYM_CHAR` | 8 bytes |
| `SYM_BOOL` | 8 bytes |
| `SYM_ARRAY` | `size * 8` bytes (min 32) |
| `SYM_STRUCT` | `field_count * 8` bytes (min 16) |
| `SYM_FUNCTION` | No stack slot (`stack_offset = -1`) |
| `SYM_POINTER` | 8 bytes |
| `SYM_C_FUNCTION` | No stack slot (`stack_offset = -1`) |

### Symbol struct fields

- `name` — variable/function name
- `type` — one of the above
- `is_external` — flag for C functions
- `c_library` — library name for externals (always NULL)
- `type_info` — union of:
  - `.array` — `{element_type, size}`
  - `.function` — `{param_types[], param_count, return_type}`
  - `.struct_instance` — `{struct_type_name}`
  - `.pointer` — `{pointed_type}`
  - `.c_function` — `{c_name, param_types[], param_count, return_type}`
- `init_value` — pointer to AST initializer node
- `stack_offset` — absolute offset from `sp` within the current frame

### SymbolTable structure

```c
typedef struct SymbolTable {
    Symbol *symbols;            // Dynamic array of symbols in this scope
    size_t count;
    size_t capacity;
    struct SymbolTable *parent; // Parent scope (for lookup chain)
    struct SymbolTable *first_child;
    struct SymbolTable *next_sibling;
    int next_offset;            // Next available stack offset
} SymbolTable;
```

Scope tree: `build_symbol_table` creates child scopes for `begin`, `if` then/else branches, `while` bodies. `find_symbol_recursive` walks up the parent chain. `get_symbol_offset` also walks parents.

**StructTypeTable:** Separate flat table of `StructType` entries (name, fields array, count). Global `global_struct_types`.

---

## Language Features

### Variables and Assignment

```clumsy
(let x int 42)          // typed declaration with initializer
(let y int)             // typed declaration, default 0
(let z 42)              // untyped, defaults to SYM_INT
(set x 99)              // assignment
(set arr[3] 42)         // array element assignment (static index)
(set arr[i] 42)         // array element assignment (dynamic index)
```

### Literals

- **Integers:** `0`, `42`, `-1`, `-100`, `0xFF`, `0x00000020`, `0x1FFF0000`
- **Strings:** `"hello"`, `"hello\n"` (escape sequences)
- **Characters:** `#\a`, `#\n` (Scheme-style)
- **Arrays:** `[1 2 3 4]` (bracket notation)
- **Struct literals:** `#((x 10) (y 20))` or `#(val1 val2)` (positional)

### Arithmetic Operators

| Operator | Description | ARM64 instruction |
|---|---|---|
| `(+ a b)` | Addition | `add` |
| `(- a b)` | Subtraction | `sub` |
| `(* a b)` | Multiplication | `mul` |
| `(/ a b)` | Signed division | `sdiv` |
| `(% a b)` | Modulo | `sdiv` + `msub` |
| `(** a b)` | Exponentiation | Emits `pow` helper (binary exponentiation loop) |

### Comparison Operators

| Operator | ARM64 condition |
|---|---|
| `(= a b)` | `eq` |
| `(< a b)` | `lt` |
| `(> a b)` | `gt` |
| `(<= a b)` | `le` |
| `(>= a b)` | `ge` |

All use `cmp` + `cset` to produce 0 or 1.

### Bitwise Operators

| Operator | Description | ARM64 instruction |
|---|---|---|
| `(& a b)` | Bitwise AND | `and` |
| `(\| a b)` | Bitwise OR | `orr` |
| `(>> a b)` | Logical shift right | `lsr` |
| `(<< a b)` | Logical shift left | `lsl` |
| `(~ a)` | Bitwise NOT (unary) | `mvn` + `and #0xFFFFFFFF` |

### Logical Operators

| Operator | Description |
|---|---|
| `(not expr)` | Logical NOT: `cmp x0, #0; cset x0, eq` |

Note: `||` and `&&` are tokenized but **not implemented** in codegen.

### Pointer Operators

| Operator | Description |
|---|---|
| `(& x)` | Address-of: computes `sp + offset` |
| `(* ptr)` | Dereference: `ldr x0, [x0]` |

Both parsed as `AST_LIST` with operator as first child. `&` and `*` are overloaded (unary = pointer ops, binary = bitwise/multiply).

### Control Flow

**if/else:**
```clumsy
(if condition then-branch)
(if condition then-branch else-branch)
```
- Condition evaluated, `cbz x0, else_label` branches on zero.
- Both branches can be single statements or `(begin ...)` blocks.

**while:**
```clumsy
(while condition body)
```
- Body must be `(begin ...)` for multiple statements.
- Standard label/bnz/b pattern.

**begin (block):**
```clumsy
(begin stmt1 stmt2 ...)
```
- Creates child scope. `let` declarations inside are local.

**ret (return):**
```clumsy
(ret expr)      // evaluate expr, jump to epilogue
(ret)           // return 0
```

### Functions

```clumsy
(let name (fn [(param1 type1) (param2 type2)] return-type body-expr))
(let name (fn [] int 42))
(let name (fn [(a int) (b int)] int (+ a b)))
```

- `fn` form recognized when followed by `[` (param bracket).
- Parameters typed: `(name type)` pairs inside `[...]`.
- Return type after closing `]`.
- Multi-expression bodies wrapped in synthetic `begin` block.
- Generated **before** `_main` in assembly output.
- Maximum 8 parameters (ARM64 ABI x0-x7).

### Function Calls

```clumsy
(function_name arg1 arg2 ...)
(add 3 4)
(puts "hello")
(SDL_Init 0x20)
```

- Arguments evaluated into x9 (scratch), spilled to stack, popped into x0-x7 before `bl`.
- C functions get `_` prefix (macOS ABI).

### C Interop (extern)

```clumsy
(extern puts [*char] int)
(extern printf [(* char) int] int)
(extern malloc [int] (* int))
(extern SDL_Init [int] int)
```

- Syntax: `(extern name [param_types...] return_type)`
- `*type` means pointer-to-type.
- All extern functions marked `SYM_C_FUNCTION`, called with `_` prefix.

### Arrays

```clumsy
(let arr int[4] [1 2 3 4])     // declaration with literal initializer
(let board int[200] [0])       // large array, shorter initializer
(arr[0])                       // read element
(arr[i])                       // read element (dynamic index)
(set arr[3] 42)               // write element (static index)
(set arr[i] 42)               // write element (dynamic index)
```

- Array type `int[size]` parsed as `AST_LIST` with `array_type` or `[]` operator.
- Each element 8 bytes on stack.
- `[]` desugared to `AST_LIST: [IDENTIFIER:"[]", array_var, index_expr]`.
- Dynamic indexing: `base + (index * 8)` at runtime.

### Structs

```clumsy
(let Point struct #((x int 10) (y int 20)))  // type definition
(let p Point #(2 3))                          // positional init
(let p Point #((x 1) (y 2)))                 // named field init
(p.x)                                         // field access
(+ p.x p.y)
```

- Stored as contiguous stack slots (8 bytes per field).
- Field lookup via `global_struct_types`.

### Print

```clumsy
(print expr)
```

- Built-in statement (not a function call).
- Auto-detects format: `%ld` for ints, `%s` for strings, `%c` for chars.
- Calls `_printf` via macOS variadic ABI: format in x0, variadic args on stack at `[sp]`.

### Comments

```clumsy
// This is a line comment
```

### Quote (unused/placeholder)

```clumsy
'expr
```
Parsed as `(quote expr)`. Not handled in compiler.

---

## Codegen Architecture (src/compiler.c, ~1725 lines)

### CodeGen Context

```c
typedef struct {
    char *output;                // Growing buffer for assembly text
    size_t output_size;
    size_t output_capacity;
    int label_counter;           // Unique label generation
    char **strings;              // String literal pool (interned)
    size_t string_count;
    size_t string_capacity;
    int sp_displacement;         // Transient sp movement from call frames
    int scratch_offset;          // Stack offset for binary expression scratch
    int scratch_depth;           // Nesting depth for scratch slot allocation
    char *current_epilogue_label; // Label target for `ret`
} CodeGen;
```

### Compilation Pipeline

1. `compile_to_arm64(ast, symbols, struct_types)` — entry point
2. `generate_preamble()` — `.text`, `.globl _main`, `.p2align 2`
3. If `**` used, emits `pow` helper function
4. **First pass:** Generates all user function definitions via `generate_function_definition()`
5. **`generate_main_function()`:**
   - Label `_main:`, frame setup `stp x29, x30, [sp, #-16]!; mov x29, sp`
   - Stack allocation: `total_tree_size(symbols)` + 128 bytes scratch, aligned to 16
   - Iterates top-level statements, skips function defs
   - Tracks "final expression" for exit code
   - Epilogue: restore stack, `ldp x29, x30, [sp], #16; ret`
6. **String pool:** `.data` section at end, `str_N: .asciz "..."`

### Function Definitions

`generate_function_definition()`:
- Label `_funcname:`
- Frame setup, function-local `SymbolTable` with `parent = symbols`
- **Two-pass params:** count first (determine register usage per type), then store from x0-x7 to stack
- Stack space via `total_tree_size()` + 128
- Body via `generate_statement()`
- `ret` statements jump to epilogue label via `b .epilogue_N`

### Variable Layout

- Each variable: 8 bytes (int, str, char, bool, pointer), `size*8` (array), `field_count*8` (struct)
- Sequential layout in AST order via `build_symbol_table`
- Stack offsets absolute from `sp`
- `sp_displacement` tracks transient stack movements

### Expression Codegen

`generate_expression()` handles:
- **AST_INT:** `movImmediate(x0, value)`
- **AST_CHAR:** `movImmediate(x0, char_value)`
- **AST_STRING:** `intern_string` + `adrp/add` into x0
- **AST_IDENTIFIER:** `emit_load_variable` — `ldr x0, [sp, #offset]`
- **AST_LIST:** Dispatches on first child:
  - Binary arithmetic/comparison/bitwise: operands into x2/x3 (scratch spilling for complex right), `emit_arithmetic`, result in x0
  - Unary `*`: evaluate, `ldr x0, [x0]`
  - Unary `&`: compute `sp + offset`
  - `not`: `cmp x0, #0; cset x0, eq`
  - `~`: `mvn x0, x0; and x0, x0, #0xFFFFFFFF`
  - `[]`: array access (static or dynamic)
  - `.`: struct field access
  - `#`: struct literal (first field only)
  - Function calls: `emit_function_call`
- **AST_ARRAY:** Only loads first element (simplified)

### Statement Codegen

`generate_statement()` handles:
- **`let`:** Eval init (or default 0), store. Special: struct defs, array init, struct init, fn defs (skipped in main, generated separately)
- **`set`:** Eval value, store. Special: `(set ([] arr idx) val)` for array element write
- **`print`:** Built-in, calls `_printf` with auto format
- **`if`:** Eval condition, `cbz` branch, then/else
- **`while`:** Label, eval condition, `cbz` exit, body, branch back
- **`begin`:** Child scope, generate each child
- **`ret`:** Eval return expr (or 0), jump to epilogue
- **Function calls:** `emit_function_call`

### Function Call Codegen

`emit_function_call()`:
- Each arg evaluated into x9, pushed to stack
- Popped into x0-x7 in reverse order
- String args: `adrp/add`
- Struct/array args: pass `sp + offset` (pointer)
- Call via `bl _funcname` or `bl _c_funcname`
- `sp_displacement` tracked through push/pop

---

## Test Inventory (31 tests)

| Test | What it tests | Has .expected |
|---|---|---|
| `basics_only.cpl` | Variables, print, arithmetic | Yes |
| `test_int_literals.cpl` | Integer literal printing | Yes |
| `test_negative_literal.cpl` | Negative number literals | Yes |
| `test_if.cpl` | If/else | Yes |
| `test_loop_basic.cpl` | While loop with begin block | Yes |
| `test_nested_scope.cpl` | Nested begin blocks | Yes |
| `test_fn_add.cpl` | Simple function definition and call | Yes |
| `test_funcall_simple.cpl` | Zero-param function call | Yes |
| `test_funcall_assignment.cpl` | Function call result assignment | Yes |
| `test_more_functions.cpl` | Multiple functions, composition | Yes |
| `test_factorial_recursive.cpl` | Recursive factorial | Yes |
| `test_factorial_iterative.cpl` | Iterative factorial with while | Yes |
| `test_ret_control.cpl` | Early return, return from loop, multiple return paths | Yes |
| `test_power.cpl` | Exponentiation operator | Yes |
| `test_bitwise.cpl` | &, \|, <<, >> | Yes |
| `test_pointer.cpl` | Pointer: declare, address-of, dereference | Yes |
| `test_pointer2.cpl` | Same as test_pointer | Yes |
| `test_deref_expr.cpl` | Dereference in expression context | Yes |
| `test_array_basic.cpl` | Array declaration, initialization, element read | Yes |
| `test_array_set.cpl` | Array element write, dynamic index, multiple arrays | Yes |
| `test_struct.cpl` | Struct type definition, instance, field access | Yes |
| `test_c_interop.cpl` | extern, pointer, printf call | Yes (compile smoke) |
| `test_c_interop_puts_string.cpl` | extern puts, string arg | Yes |
| `test_c_interop_multi_args.cpl` | Multiple puts calls | Yes |
| `test_c_interop_puts_null.cpl` | puts with integer 0 | Yes |
| `test_bug_simple.cpl` | Bug regression (single puts) | Yes |
| `test_bug_statement_processing.cpl` | Bug regression (statement processing) | Yes |
| `test_bug_not_last.cpl` | Bug regression (function call not last) | Yes |
| `test_bug_no_calls.cpl` | Bug regression (no function calls) | Yes |
| `test_no_sdl.cpl` | Compile smoke with extern SDL types | Yes |
| `test_multi_args.cpl` | Multiple print calls | No |

---

## Known Limitations

### Language-level

1. No `||` or `&&` — tokenized but not implemented. No short-circuit.
2. No `!=` operator — must use `(not (= a b))`.
3. No string comparison.
4. No closures / first-class functions.
5. No mutual recursion (functions emitted in order, forward refs fail).
6. No `for` loop — must use `while` + counter.
7. No `break` or `continue` in loops.
8. No `elif`.
9. No multi-line strings or string interpolation.
10. `null` keyword tokenized but not implemented; pointer nulls are `0`.
11. `include` keyword tokenized but not implemented.
12. No floating-point support — everything is integer.
13. `AST_ADDRESS_OF` / `AST_DEREFERENCE` defined but never used by parser.
14. Quote (`'expr`) parsed but never compiled.
15. Struct literals in expression context only return first field.
16. No struct field assignment (`set p.x 10` not handled).
17. No nested structs — fields must be primitive types.
18. No arrays of strings or arrays of structs.
19. Array type syntax inconsistent (`(* int)[4]` doesn't work).
20. Extern param types partially parsed — `param_types` not fully populated.

### Codegen-level

21. Only 64-bit integers — no 32-bit specialization.
22. Scratch space fixed at 128 bytes — deeply nested expressions could overflow.
23. Function params at hardcoded offsets (not using normal symbol table).
24. No callee-saved register usage — only x0-x9 scratch.
25. No stack alignment verification for nested calls.
26. String escaping in `intern_string` incomplete — works by accident (`.asciz` interprets C escapes).
27. Struct type definitions global (`global_struct_types` static) — not scoped.
28. No struct field assignment (only whole-variable).
29. `AST_ARRAY` in expression context only returns first element.
30. No error recovery — unrecognized syntax causes `exit(1)`.
