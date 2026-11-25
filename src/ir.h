#ifndef IR_H
#define IR_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "types.h"
#include "arena.h"

// --- IR Types ---

typedef enum {
    IR_TYPE_VOID,
    IR_TYPE_I64,
    IR_TYPE_PTR,
} IRType;

// --- IR Opcodes ---

typedef enum {
    // Arithmetic (produce i64)
    IR_ADD,
    IR_SUB,
    IR_MUL,
    IR_DIV,
    IR_MOD,
    IR_AND,
    IR_OR,
    IR_XOR,
    IR_SHL,
    IR_SHR,
    IR_NOT,    // bitwise NOT
    IR_NEG,    // arithmetic negation (-x)

    // Comparisons (produce 0 or 1)
    IR_EQ,
    IR_NE,
    IR_LT,
    IR_GT,
    IR_LE,
    IR_GE,

    // Memory
    IR_LOAD,     // %x = load ptr, offset
    IR_STORE,    // store val, ptr, offset
    IR_ALLOC,    // %x = alloc size bytes on stack

    // Control
    IR_JMP,      // jmp @label
    IR_JNZ,      // jnz cond, @then, @else
    IR_RET,      // ret val (or ret void)

    // Calls
    IR_CALL,     // %x = call $func(args...)

    // SSA (for later)
    IR_PHI,      // %x = phi [(%v1, @b1), (%v2, @b2)]

    // Constants
    IR_MOV,      // %x = mov val (immediate or register)

    // Address
    IR_ADDR,     // %x = addr $label (address of data/string/function)
} IROpcode;

// --- IR Operands ---

typedef enum {
    OP_NONE,
    OP_IMM,      // immediate integer constant
    OP_REGISTER, // virtual register (%name)
    OP_VREG,     // SSA virtual register (%v_N)
    OP_BLOCK,    // basic block label (@label)
    OP_GLOBAL,   // global symbol ($name)
} IROperandType;

typedef struct {
    IROperandType type;
    union {
        int64_t imm;
        char *reg;       // virtual register name
        char *block;     // basic block label
        char *global;    // global symbol (function, string data)
    };
} IROperand;

// --- IR Instructions ---

typedef struct {
    IROpcode op;
    IROperand dest;        // result register (or OP_NONE)
    IROperand args[8];     // up to 8 source operands (func + 7 args for calls)
    IROperand phi_pairs[8]; // for IR_PHI: [val, block, val, block, ...]
    int phi_count;
    IRType dest_type;      // type of result (i64 or ptr)
    bool is_variadic;      // for IR_CALL: use ... for macOS ABI
} IRInstr;

// --- Basic Blocks ---

typedef struct IRBlock {
    char *label;
    IRInstr *instrs;
    int num_instrs;
    int capacity;
    struct IRBlock **succs;   // successor blocks (CFG edges)
    int num_succs;
    int succ_capacity;
    struct IRBlock **preds;   // predecessor blocks (CFG edges)
    int num_preds;
    int pred_capacity;

    // Dominator tree (computed later in Phase 2)
    struct IRBlock *idom;
    struct IRBlock **dom_children;
    int num_dom_children;

    // Dominance frontiers (set of blocks where this block's dominator frontier is)
    struct IRBlock **dom_frontiers;
    int num_dom_frontiers;
    int df_capacity;

    // SSA: predecessors in the CFG (for phi argument construction)
    struct IRBlock **phi_preds;   // preds that have phi args for this block
    int num_phi_preds;

    // Block index in RPO order (computed by dominator algorithm)
    int rpo_index;
} IRBlock;

// --- IR Functions ---

typedef struct {
    char *name;
    IRType return_type;
    IRBlock *entry;
    IRBlock **blocks;
    int num_blocks;
    int block_capacity;

    // Parameters (virtual register names)
    char **params;
    IRType *param_types;
    int num_params;

    // String literals (emitted as data directives)
    struct {
        char *label;
        char *value;
    } *strings;
    int num_strings;
    int string_capacity;

    // Label counter for generating unique labels
    int label_counter;

    // SSA: virtual register counter
    int vreg_counter;

    bool is_public;   // pub: export with 'export' keyword
} IRFunction;

// --- IR Program ---

typedef struct {
    IRFunction **functions;
    int num_functions;
    int func_capacity;

    // Extern declarations (C functions)
    struct {
        char *name;
        IRType return_type;
        IRType *param_types;
        int num_params;
        bool is_variadic;
    } *externs;
    int num_externs;
    int extern_capacity;

    // Global data
    struct {
        char *name;
        int64_t value;
        bool is_string;
        char *str_val;
        int array_size;  // 0 for scalars, >0 for arrays
        bool is_public;
        int64_t *array_values; // non-NULL for arrays: all element values
        // Struct field tracking
        char **struct_fields;
        int num_struct_fields;
        bool is_struct;
        bool is_extern;  // variable extern: declaration only, no data emission
    } *globals;
    int num_globals;
    int global_capacity;
} IRProgram;

// --- Construction Helpers ---

// Operands
IROperand op_none(void);
IROperand op_imm(int64_t value);
IROperand op_reg(const char *name);
IROperand op_vreg(int id);
IROperand op_block(const char *label);
IROperand op_global(const char *name);

// Instructions
IRInstr ir_instr(IROpcode op, IROperand dest, IROperand a0, IROperand a1, IROperand a2, IRType dest_type);
IRInstr ir_call(IROperand dest, IROperand *args, int num_args, IRType dest_type);
IRInstr ir_phi(IROperand dest, IROperand *pairs, int count, IRType dest_type);

// Basic blocks
IRBlock *ir_new_block(const char *label);
void ir_append_instr(IRBlock *block, IRInstr instr);

// SSA helpers
IROperand ir_new_vreg(IRFunction *fn);

// Functions
IRFunction *ir_new_function(const char *name, IRType return_type);
IRBlock *ir_add_block(IRFunction *fn, const char *label);
void ir_set_entry(IRFunction *fn, IRBlock *block);
char *ir_gen_label(IRFunction *fn, const char *prefix);

// Programs
IRProgram *ir_new_program(void);
IRFunction *ir_add_function(IRProgram *prog, const char *name, IRType ret);
void ir_add_extern(IRProgram *prog, const char *name, IRType ret, IRType *params, int num_params, bool variadic);
void ir_add_string(IRFunction *fn, const char *label, const char *value);
void ir_add_global(IRProgram *prog, const char *name, int64_t value, bool is_string, const char *str_val, int array_size, bool is_public);
void ir_add_global_array(IRProgram *prog, const char *name, int64_t *values, int array_size, bool is_public);
void ir_add_struct_global(IRProgram *prog, const char *name, char **fields, int num_fields, bool is_public);

// Cleanup
void free_ir_program(IRProgram *prog);

// --- String helpers ---
char *ir_strdup(Arena *a, const char *s);

// --- Printing (for debugging) ---
void ir_print_program(IRProgram *prog, FILE *out);
void ir_print_function(IRFunction *fn, FILE *out);
void ir_print_block(IRBlock *block, FILE *out);
void ir_print_instr(IRInstr *instr, FILE *out);

const char *ir_opcode_name(IROpcode op);
const char *ir_type_name(IRType type);

// --- QBE Emission ---
void emit_qbe(IRProgram *prog, FILE *out);

// --- LLVM IR Emission ---
void emit_llvm(IRProgram *prog, FILE *out);

// --- Cranelift CLIF Emission ---
void emit_cranelift(IRProgram *prog, FILE *out);

// --- AST → IR Translation ---
IRProgram *ir_build(ASTNode *ast, SymbolTable *symbols);

// --- Variadic detection (shared by qbe/llvm emitters and stub emission) ---
bool ir_call_is_variadic(IRProgram *prog, IRInstr *in);
bool ir_extern_is_variadic(IRProgram *prog, const char *name);
bool ir_program_calls(IRProgram *prog, const char *name);

// --- Variadic-call stubs (Apple AArch64) ---
void emit_variadic_stubs(IRProgram *prog, FILE *out);

#endif // IR_H
