A language that's statically typed and compiles to native code.

- basic types: string, int, characters, arrays, pointers
- compound types: structs 
- branching: loops, decisions, functions
- std library: print, clock, ...
- call any c/asm function: c-interop

## Syntax

```
// variable initialization with optional initializer
(let shouldPrint bool)
(let age int 123)
(let welcome str "hello world\n")
(let initial char #\q)
(let greeting *str &welcome)

// arrays
(let firstNaturalNumbers int[4] [1 2 3 4])
firstNaturalNumbers[2]

// structs
// TODO: (struct amoundAndInitial...)
(let amountAndInitial struct 
    #((amount int 356)
      (initial char #\c)))

// variable mutation
(set initial #\r)
(let getA 
    (fn getA () 
        char
        (begin
            // ...
            (ret #\s))))
(set initial (getA))

// nested structures
(let nestedInitial struct
  #((amI amountAndInitial (789 #\e))))
  
// functions
// TODO: (fn functionExample ...)
(let functionExample 
    (fn
        [(p1 int)
         (p2 str)
         (p3 int[10])]
        int
        (begin
            (print "computing: %s" p2) // only builtin for now
            (ret (* p1 p3[4])))))
      
// C-interop - TODO
// (let puts 
//    (fn
//      [(in str)]
//    void)) // no body, just a declaration
    
// FUNCTION CALL
(functionExample 12 "hello" [1 3 5 7 9 11 13 15 17 19])

// CONDITION
(if (== age 123)
  // do something
  (print age)

  // do something else
  (print 123))

// LOOP
(while (<= age 142)
  (set age (+ age 1))
  // do something
  )

// PROPERTY access from structs
(let something amountAndInitial #(211 #\f))
something.amount
something.initial
```

## Implementations

- Compiler for armv8

## PROGRESS

  | Feature                 | Compiler (ARM64) | Compiler (x86) |
  |-------------------------|------------------|----------------|
  | Basic Variables         | ✅                |                |
  | Arithmetic (+,-,\*,/)   | ✅                |                |
  | Modulo (%)              | ✅                |                |
  | Exponentiation (**)     | ✅                |                |
  | Comparisons             | ✅                |                |
  | Arrays                  | ✅                |                |
  | Control Flow (if/while) | ✅                |                |
  | Print Statements        | ✅                |                |
  | Function Definitions    | ✅                |                |
  | Function Calls          | ✅                |                |
  | Structs                 | ✅                |                |
  | Property Access (.)     | ✅                |                |
  | C-Interop               | ❌                |                |


## TODO

### bugs

- broken functions, arrays and structs
- simpler struct and function declaration syntax


### features
- null type, void type
- pointer types
- ffi with C
- heap allocation
- bitfields & operators

### misc
- sample programs
- data structure libraries
- modules? - compile each file to .o and link separately for now?

### targets
- compile to QBE, LLVM, x86-64 and RISC-V
- ast-walking & bytecode interpreter + stack machine

### optimizations
- optimization passes from CS6120 for the compiler

### syntax
- different syntaxes (specify as JSON, imperative?)

## Security & Production Readiness
- Add file size limits to prevent potential overflow on very large source files
- Enhanced input validation for source file content structure
- Sanitize error messages to prevent format string vulnerabilities with user-controlled filenames
- Add comprehensive error handling for edge cases in tokenizer escape sequence processing
- Implement maximum parsing depth limits to prevent stack overflow on deeply nested expressions

## BUGS

## TODO

- refactor for understandability
