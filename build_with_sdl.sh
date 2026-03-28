#!/bin/bash

# Build script for Clumsy programs with proper SDL linking

if [ $# -ne 1 ]; then
    echo "Usage: $0 <source_file.cpl>"
    exit 1
fi

SOURCE_FILE=$1
BASE_NAME=$(basename "$SOURCE_FILE" .cpl)
ASM_FILE="${BASE_NAME}.s"
OBJ_FILE="${BASE_NAME}.o"
PRINT_HELPERS_OBJ="print_helpers.o"

echo "=== Building $SOURCE_FILE ==="

# Step 1: Compile to assembly
echo "1. Compiling to assembly..."
./build/clumsyc "$SOURCE_FILE" > "$ASM_FILE"
if [ $? -ne 0 ]; then
    echo "   ERROR: Compilation failed"
    exit 1
fi

# Step 2: Assemble to object file
echo "2. Assembling to object file..."
as -o "$OBJ_FILE" "$ASM_FILE"
if [ $? -ne 0 ]; then
    echo "   ERROR: Assembly failed"
    exit 1
fi

# Step 3: Compile print helpers if needed
if [ ! -f "$PRINT_HELPERS_OBJ" ]; then
    echo "3. Compiling print helpers..."
    gcc -c src/print_helpers.c -o "$PRINT_HELPERS_OBJ"
    if [ $? -ne 0 ]; then
        echo "   ERROR: Print helpers compilation failed"
        exit 1
    fi
fi

# Step 4: Link with SDL, print helpers, and C standard library
echo "4. Linking with SDL, print helpers, and C standard library..."
SDL_CFLAGS=$(sdl2-config --cflags)
SDL_LIBS=$(sdl2-config --libs)

clang $SDL_CFLAGS "$OBJ_FILE" "$PRINT_HELPERS_OBJ" $SDL_LIBS -o "$BASE_NAME"
if [ $? -ne 0 ]; then
    echo "   ERROR: Linking failed"
    exit 1
fi

echo "=== Build complete: $BASE_NAME ==="
echo "   Run with: ./$BASE_NAME"