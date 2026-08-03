#include <stdbool.h>
#include "types.h"
#include "tokenizer.h"
#include "parser.h"
#include "compiler.h"
#include "ir.h"
#include "cfg.h"

typedef enum {
    BACKEND_ARM64,
    BACKEND_QBE,
    BACKEND_LLVM
} Backend;

void usage(const char *program_name) {
    fprintf(stderr, "usage: %s [--debug] [--dump-cfg] [--dump-ssa] [--backend=aarch64-legacy|qbe|llvm] <source_file>\n", program_name);
    fprintf(stderr, "compile clumsy to assembly\n");
    fprintf(stderr, "options:\n");
    fprintf(stderr, "  --debug                              print syntax tree and symbol table to stderr\n");
    fprintf(stderr, "  --dump-cfg                           print CFG as Graphviz DOT to stdout\n");
    fprintf(stderr, "  --dump-ssa                           run SSA pipeline and print IR to stderr\n");
    fprintf(stderr, "  --backend=aarch64-legacy|qbe|llvm    select backend (default: qbe)\n");
    exit(1);
}

char *read_file(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "error: cannot open file '%s'\n", filename);
        exit(1);
    }
    
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    if (file_size < 0) {
        fprintf(stderr, "error: cannot determine size of '%s'\n", filename);
        fclose(file);
        exit(1);
    }
    fseek(file, 0, SEEK_SET);
    
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
    bool dump_cfg = false;
    bool dump_ssa = false;
    Backend backend = BACKEND_QBE;
    const char *source_file = NULL;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        } else if (strcmp(argv[i], "--dump-cfg") == 0) {
            dump_cfg = true;
        } else if (strcmp(argv[i], "--dump-ssa") == 0) {
            dump_ssa = true;
        } else if (strncmp(argv[i], "--backend=", 10) == 0) {
            const char *name = argv[i] + 10;
            if (strcmp(name, "aarch64-legacy") == 0 || strcmp(name, "arm64") == 0) {
                backend = BACKEND_ARM64;
            } else if (strcmp(name, "qbe") == 0) {
                backend = BACKEND_QBE;
            } else if (strcmp(name, "llvm") == 0) {
                backend = BACKEND_LLVM;
            } else {
                fprintf(stderr, "error: unknown backend '%s'\n", name);
                usage(argv[0]);
            }
        } else if (source_file == NULL) {
            source_file = argv[i];
        } else {
            usage(argv[0]);
        }
    }
    
    if (source_file == NULL) {
        usage(argv[0]);
    }
    
    char *source_code = read_file(source_file);
    
    TokenArray *tokens = tokenize(source_code);
    if (!tokens) {
        fprintf(stderr, "error: tokenization failed\n");
        free(source_code);
        exit(1);
    }
    
    ASTNode *ast = parse(tokens);
    if (!ast) {
        fprintf(stderr, "error: parsing failed\n");
        free_token_array(tokens);
        free(source_code);
        exit(1);
    }
    
    SymbolTable *symbols = build_symbol_table(ast);
    if (!symbols) {
        fprintf(stderr, "error: symbol table construction failed\n");
        free_ast(ast);
        free_token_array(tokens);
        free(source_code);
        exit(1);
    }

    if (debug) {
        fprintf(stderr, "ast:\n");
        print_ast(ast, 0);
        fprintf(stderr, "symbol table:\n");
        print_symbol_table(symbols);
    }
    
    if (backend == BACKEND_QBE || backend == BACKEND_LLVM) {
        IRProgram *prog = ir_build(ast, symbols);
        if (!prog) {
            fprintf(stderr, "error: IR construction failed\n");
            free_symbol_table(symbols);
            free_ast(ast);
            free_token_array(tokens);
            free(source_code);
            exit(1);
        }

        if (debug) {
            fprintf(stderr, "ir:\n");
            ir_print_program(prog, stderr);
        }

        // Run SSA pipeline on all functions (dominators + rename)
        for (int fi = 0; fi < prog->num_functions; fi++) {
            cfg_ssa_pipeline(prog->functions[fi]);
        }

        if (dump_cfg) {
            for (int fi = 0; fi < prog->num_functions; fi++) {
                cfg_dump_dot(prog->functions[fi], stdout, CFG_DOT_CFG);
                fprintf(stdout, "\n");
            }
            free_ir_program(prog);
            free_symbol_table(symbols);
            free_ast(ast);
            free_token_array(tokens);
            free(source_code);
            return 0;
        }

        if (dump_ssa) {
            for (int fi = 0; fi < prog->num_functions; fi++) {
                IRFunction *fn = prog->functions[fi];
                fprintf(stderr, "=== %s (SSA) ===\n", fn->name);
                ir_print_function(fn, stderr);
                fprintf(stderr, "\n--- Dominator Tree ---\n");
                cfg_dump_dot(fn, stderr, CFG_DOT_DOM);
                fprintf(stderr, "\n--- Dominance Frontiers ---\n");
                cfg_compute_dominance_frontiers(fn);
                cfg_dump_dot(fn, stderr, CFG_DOT_DF);
            }
            free_ir_program(prog);
            free_symbol_table(symbols);
            free_ast(ast);
            free_token_array(tokens);
            free(source_code);
            return 0;
        }

        if (backend == BACKEND_QBE) {
            emit_qbe(prog, stdout);
        } else {
            emit_llvm(prog, stdout);
        }
        fflush(stdout);
        free_ir_program(prog);
        free_symbol_table(symbols);
        free_ast(ast);
        free_token_array(tokens);
        free(source_code);
        return 0;
    } else {
        char *assembly = compile_to_arm64(ast, symbols, NULL);
        if (!assembly) {
            fprintf(stderr, "error: code generation failed\n");
            free_symbol_table(symbols);
            free_ast(ast);
            free_token_array(tokens);
            free(source_code);
            exit(1);
        }
        printf("%s", assembly);
        free(assembly);
    }
    
    free_symbol_table(symbols);
    free_ast(ast);
    free_token_array(tokens);
    free(source_code);
    
    return 0;
}
