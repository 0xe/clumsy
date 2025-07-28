#include <stdbool.h>
#include "types.h"
#include "tokenizer.h"
#include "parser.h"
#include "compiler.h"

void usage(const char *program_name) {
    fprintf(stderr, "Usage: %s [--ast] <source_file>\n", program_name);
    fprintf(stderr, "Compiles Cimple source code to ARM64 assembly\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --ast    Print the Abstract Syntax Tree and exit\n");
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
    bool print_ast_flag = false;
    const char *source_file = NULL;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ast") == 0) {
            print_ast_flag = true;
        } else if (source_file == NULL) {
            source_file = argv[i];
        } else {
            usage(argv[0]);
        }
    }
    
    if (source_file == NULL) {
        usage(argv[0]);
    }
    
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
    
    // If --ast flag is set, print AST and exit
    if (print_ast_flag) {
        printf("Abstract Syntax Tree:\n");
        print_ast(ast, 0);
        
        // Cleanup and exit
        free_ast(ast);
        free_token_array(tokens);
        free(source_code);
        return 0;
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