A few experiments in language implementation.
A simple lisp-like language that's statically typed and compiles to native code.

## Syntax

```
// LET name type init
(let y bool)
(let x int 123)
(let z str "hello world\n")
(let a char #\q)
(let b int[4] [1 2 3 4])
b[2]
(let c struct 
    #((a int 356)
      (b char #\c)))

// SET name value
(set a #\r)
(let getA 
    (fn () 
        char
        (begin
            // ...
            (ret #\s))))
            
(set a (getA))
(let d struct
  #((d1 c (789 #\e))))
  
(let e 
    (fn
        [(p1 int)
         (p2 str)
         (p3 int[10])]
        int
        (begin
            (print "computing: %s" p2) // only builtin for now
            (ret (* p1 p3[4])))))
      
// C-interop - TODO
(let puts 
    (fn
      [(in str)]
    void)) // no body, just a declaration
    
// FUNCTION CALL
(e 12 "hello" [1 3 5 7 9 11 13 15 17 19])

// CONDITION
(if (== x 123)
  // do something
  (print x)

  // do something else
  (print 123))

// LOOP
(while (<= x 142)
  (set x (+ x 1))
  // do something
  )

// PROPERTY access from structs
(let k d #(211 #\f))
k.d1.a
k.d1.b
```

## Implementations

- Compiler for armv8

## PROGRESS

  | Feature                 | Compiler (ARM64) | Compiler (x86) |
  |-------------------------|------------------|----------------|
  | Basic Variables         | ✅               |                |
  | Arithmetic (+,-,\*,/)   | ✅               |                |
  | Modulo (%)              | ✅               |                |
  | Exponentiation (**)     | ✅               |                |
  | Comparisons             | ✅               |                |
  | Arrays                  | ✅               |                |
  | Control Flow (if/while) | ✅               |                |
  | Print Statements        | ✅               |                |
  | Function Definitions    | ✅               |                |
  | Function Calls          | ✅               |                |
  | Structs                 | ✅               |                |
  | Property Access (.)     | ✅               |                |
  | C-Interop               | ❌               |                |


## TODO

- null type, void type
- pointer types
- ffi with C
- heap allocation
- sample programs
- data structure libraries
- bitfields
- bitwise operators etc.
- modules - compile each file to .o and link separately for now?
- Compile to QBE, LLVM, ArmV8, x86-64 and RISC-V (emits assembly/IR)
- Optimization passes from CS6120 for the compiler

### Security & Production Readiness
- Add file size limits to prevent potential overflow on very large source files
- Enhanced input validation for source file content structure
- Sanitize error messages to prevent format string vulnerabilities with user-controlled filenames
- Add comprehensive error handling for edge cases in tokenizer escape sequence processing
- Implement maximum parsing depth limits to prevent stack overflow on deeply nested expressions

## BUGS

