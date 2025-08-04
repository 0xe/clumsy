#include <stdbool.h>
#include "types.h"
#include "tokenizer.h"
#include "parser.h"
#include "compiler.h"

void usage(const char *program_name) {
    fprintf(stderr, "usage: %s [--debug] <source_file>\n", program_name);
    fprintf(stderr, "compile clumsy to ARM64 assembly\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  --debug    print syntax tree and symbol table to stderr\n");
    exit(1);
}

char *read_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "error: cannot open file '%s'\n", filename);
        exit(1);
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    // Allocate buffer and read file
    char *content = malloc(file_size + 1);
    if (!content) {
        fprintf(stderr, "error: failed to allocate memory for file content\n");
        fclose(file);
        exit(1);
    }
    
    size_t bytes_read = fread(content, 1, file_size, file);
    content[bytes_read] = '\0';
    
    fclose(file);
    return content;
}

int main(int argc, char *argv[]) {
    bool debug = false;
    const char *source_file = NULL;
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
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
        fprintf(stderr, "error: tokenization failed\n");
        free(source_code);
        exit(1);
    }
    
    // Parse
    ASTNode *ast = parse(tokens);
    if (!ast) {
        fprintf(stderr, "error: parsing failed\n");
        free_token_array(tokens);
        free(source_code);
        exit(1);
    }
    
    // Build symbol table
    SymbolTable *symbols = build_symbol_table(ast);
    if (!symbols) {
        fprintf(stderr, "error: symbol table construction failed\n");
        free_ast(ast);
        free_token_array(tokens);
        free(source_code);
        exit(1);
    }

    // If --debug flag is set, print AST and exit
    if (debug) {
        fprintf(stderr, "ast:\n");
        print_ast(ast, 0);
        
		fprintf(stderr, "symbol table:\n");
		print_symbol_table(symbols);
    }
    
    // Compile to ARM64
    char *assembly = compile_to_arm64(ast, symbols, NULL);
    if (!assembly) {
        fprintf(stderr, "error: code generation failed\n");
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
