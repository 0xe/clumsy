#include "types.h"
#include "tokenizer.h"
#include "parser.h"
#include "compiler.h"

void usage(const char *program_name) {
    fprintf(stderr, "Usage: %s <source_file>\n", program_name);
    fprintf(stderr, "Compiles Cimple source code to ARM64 assembly\n");
    exit(1);
}

char *read_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        exit(1);
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Allocate buffer and read file
    char *content = malloc(file_size + 1);
    if (!content) {
        fprintf(stderr, "Error: Failed to allocate memory for file content\n");
        fclose(file);
        exit(1);
    }
    
    size_t bytes_read = fread(content, 1, file_size, file);
    content[bytes_read] = '\0';
    
    fclose(file);
    return content;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        usage(argv[0]);
    }
    
    const char *source_file = argv[1];
    
    // Read source file
    char *source_code = read_file(source_file);
    
    // Tokenize
    TokenArray *tokens = tokenize(source_code);
    if (!tokens) {
        fprintf(stderr, "Error: Tokenization failed\n");
        free(source_code);
        exit(1);
    }
    
    // Parse
    ASTNode *ast = parse(tokens);
    if (!ast) {
        fprintf(stderr, "Error: Parsing failed\n");
        free_token_array(tokens);
        free(source_code);
        exit(1);
    }
    
    // Build symbol table
    SymbolTable *symbols = build_symbol_table(ast);
    if (!symbols) {
        fprintf(stderr, "Error: Symbol table construction failed\n");
        free_ast(ast);
        free_token_array(tokens);
        free(source_code);
        exit(1);
    }
    
    // Compile to ARM64
    char *assembly = compile_to_arm64(ast, symbols);
    if (!assembly) {
        fprintf(stderr, "Error: Code generation failed\n");
        free_symbol_table(symbols);
        free_ast(ast);
        free_token_array(tokens);
        free(source_code);
        exit(1);
    }
    
    // Output assembly
    printf("%s", assembly);
    
    // Cleanup
    free(assembly);
    free_symbol_table(symbols);
    free_ast(ast);
    free_token_array(tokens);
    free(source_code);
    
    return 0;
}