#ifndef EMIT_COMMON_H
#define EMIT_COMMON_H

#include "hashmap.h"
#include "ir.h"

typedef struct {
    StringIntMap externs;
    StringIntMap functions;
    StringIntMap globals;
} IRIndex;

void ir_index_init(IRIndex *index, IRProgram *prog, Arena *arena);
int ir_index_extern(IRIndex *index, const char *name);
int ir_index_function(IRIndex *index, const char *name);
int ir_index_global(IRIndex *index, const char *name);

char *emit_sanitize(const char *name);
char *emit_sanitize_llvm(const char *name);

const char *emit_unquote(const char *value, int *out_len);
int emit_decode_byte(const char *value, int len, int i, unsigned char *out);
int emit_string_byte_count(const char *value);
int emit_string_bytes(const char *value, unsigned char *buf, int buf_cap);

#endif
