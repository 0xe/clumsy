# Cimple C Compiler

C implementation of the Cimple language ARM64 compiler.

## Components

- `tokenizer.c/h` - Lexical analysis (equivalent to `int/tokenizer.py`)
- `parser.c/h` - Syntax analysis (equivalent to `int/frontend.py`)
- `compiler.c/h` - ARM64 code generation (equivalent to `int/compiler.py`)
- `main.c` - Entry point and CLI interface
- `types.h` - Common data structures and type definitions

## Build

```bash
make
```

## Usage

```bash
./cimple_comp source.cpl > output.s
```

## Architecture

The C compiler follows the same structure as the Python version:
1. Tokenize source code into tokens
2. Parse tokens into AST
3. Generate ARM64 assembly from AST