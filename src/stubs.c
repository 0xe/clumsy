#include "ir.h"
#include "arena.h"

// Variadic-call stubs for the QBE and LLVM backends (Apple AArch64).
//
// On Apple's AArch64 ABI, variadic arguments are passed on the stack: the
// first variadic arg sits at [sp], then [sp+8], etc. (the callee reads them
// from there), and x8 must hold the count of vector args passed. Neither QBE
// nor LLVM's variadic lowering handles this convention, so calls to variadic
// externs are re-targeted at a fixed-arity `<name>_stub` shim: the caller
// passes every arg in x0-x7 (the stub pads short calls with zeros), and the
// stub shuffles x1-x7 down to the stack, zeroes x8, and calls the real
// function. This mirrors the cranelift backend, whose clifc emits the same
// stubs for `;; variadic` externs.

static void emit_stub(FILE *out, const char *name) {
    // macOS prepends '_' to assembly symbols.
    fprintf(out, "    .text\n");
    fprintf(out, "    .p2align    2\n");
    fprintf(out, "    .globl  _%s_stub\n", name);
    fprintf(out, "    .weak_definition _%s_stub\n", name);
    fprintf(out, "_%s_stub:\n", name);
    fprintf(out, "    stp x29, x30, [sp, #-16]!\n");
    fprintf(out, "    mov x29, sp\n");
    fprintf(out, "    sub sp, sp, #64\n");
    fprintf(out, "    str x1, [sp]\n");
    fprintf(out, "    str x2, [sp, #8]\n");
    fprintf(out, "    str x3, [sp, #16]\n");
    fprintf(out, "    str x4, [sp, #24]\n");
    fprintf(out, "    str x5, [sp, #32]\n");
    fprintf(out, "    str x6, [sp, #40]\n");
    fprintf(out, "    str x7, [sp, #48]\n");
    fprintf(out, "    mov x8, #0\n");
    fprintf(out, "    bl   _%s\n", name);
    fprintf(out, "    add sp, sp, #64\n");
    fprintf(out, "    ldp x29, x30, [sp], #16\n");
    fprintf(out, "    ret\n");
    fprintf(out, "\n");
}

void emit_variadic_stubs(IRProgram *prog, FILE *out) {
    arena_reset(&g_arena_scratch);

    // Collect the distinct variadic externs the program actually calls (every
    // extern with a variadic call site, plus printf which is always libc-variadic).
    char **stubs = NULL;
    int num_stubs = 0;
    int cap = 0;

    for (int i = 0; i < prog->num_externs; i++) {
        const char *name = prog->externs[i].name;
        // Only emit a stub if the program references it: files that merely
        // declare printf (or none at all) must not define a duplicate stub.
        if (!ir_extern_is_variadic(prog, name)) continue;
        if (!ir_program_calls(prog, name)) continue;
        if (num_stubs >= cap) {
            int old_cap = cap;
            cap = cap ? cap * 2 : 8;
            stubs = arena_grow(&g_arena_scratch, stubs,
                               (size_t)old_cap * sizeof(char *),
                               (size_t)cap * sizeof(char *));
        }
        stubs[num_stubs] = ir_strdup(&g_arena_scratch, name);
        num_stubs++;
    }

    // Undeclared printf (print/println builtin) still needs a stub.
    if (ir_program_calls(prog, "printf") && ir_extern_is_variadic(prog, "printf")) {
        int seen = 0;
        for (int i = 0; i < num_stubs; i++) {
            if (strcmp(stubs[i], "printf") == 0) { seen = 1; break; }
        }
        if (!seen) {
            if (num_stubs >= cap) {
                int old_cap = cap;
                cap = cap ? cap * 2 : 8;
                stubs = arena_grow(&g_arena_scratch, stubs,
                                   (size_t)old_cap * sizeof(char *),
                                   (size_t)cap * sizeof(char *));
            }
            stubs[num_stubs] = ir_strdup(&g_arena_scratch, "printf");
            num_stubs++;
        }
    }

    for (int i = 0; i < num_stubs; i++) {
        emit_stub(out, stubs[i]);
    }
}
