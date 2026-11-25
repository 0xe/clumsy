#include "emit_common.h"
#include <ctype.h>

void ir_index_init(IRIndex *index, IRProgram *prog, Arena *arena) {
    str_int_map_init(&index->externs, arena, (size_t)prog->num_externs * 2);
    str_int_map_init(&index->functions, arena, (size_t)prog->num_functions * 2);
    str_int_map_init(&index->globals, arena, (size_t)prog->num_globals * 2);

    for (int i = 0; i < prog->num_externs; i++) {
        str_int_map_put(&index->externs, prog->externs[i].name, i);
    }
    for (int i = 0; i < prog->num_functions; i++) {
        str_int_map_put(&index->functions, prog->functions[i]->name, i);
    }
    for (int i = 0; i < prog->num_globals; i++) {
        if (prog->globals[i].is_struct) continue;
        str_int_map_put(&index->globals, prog->globals[i].name, i);
    }
}

static int map_lookup(StringIntMap *map, const char *name) {
    int *value = str_int_map_get(map, name);
    return value ? *value : -1;
}

int ir_index_extern(IRIndex *index, const char *name) {
    return map_lookup(&index->externs, name);
}

int ir_index_function(IRIndex *index, const char *name) {
    return map_lookup(&index->functions, name);
}

int ir_index_global(IRIndex *index, const char *name) {
    return map_lookup(&index->globals, name);
}

static char *sanitize_with(const char *name, bool llvm) {
    int len = (int)strlen(name);
    char *result = arena_strdup(&g_arena_scratch, name);
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)result[i];
        bool allowed = isalnum(c) || c == '_';
        if (llvm) allowed = allowed || c == '$' || c == '.' || c == '-';
        if (!allowed) result[i] = '_';
    }
    return result;
}

char *emit_sanitize(const char *name) {
    return sanitize_with(name, false);
}

char *emit_sanitize_llvm(const char *name) {
    return sanitize_with(name, true);
}

const char *emit_unquote(const char *value, int *out_len) {
    int len = (int)strlen(value);
    if (len >= 2 && value[0] == '"' && value[len - 1] == '"') {
        *out_len = len - 2;
        return value + 1;
    }
    *out_len = len;
    return value;
}

int emit_decode_byte(const char *value, int len, int i, unsigned char *out) {
    unsigned char c = (unsigned char)value[i];
    if (c == '\\' && i + 1 < len) {
        char escape = value[i + 1];
        int next = i + 2;
        switch (escape) {
            case 'n': *out = '\n'; return next;
            case 't': *out = '\t'; return next;
            case 'r': *out = '\r'; return next;
            case 'a': *out = '\a'; return next;
            case 'b': *out = '\b'; return next;
            case 'f': *out = '\f'; return next;
            case 'v': *out = '\v'; return next;
            case '\\': *out = '\\'; return next;
            case '"': *out = '"'; return next;
            case '\'': *out = '\''; return next;
            case 'x': {
                int val = 0, have = 0;
                while (next < len && isxdigit((unsigned char)value[next])) {
                    val = val * 16 + (isdigit((unsigned char)value[next])
                                          ? value[next] - '0'
                                          : tolower((unsigned char)value[next]) - 'a' + 10);
                    next++;
                    have = 1;
                }
                *out = have ? (unsigned char)val : 'x';
                return next;
            }
            default: {
                int val = 0, have = 0;
                if (escape >= '0' && escape <= '7') {
                    val = escape - '0';
                    have = 1;
                    while (next < len && have < 3 && value[next] >= '0' && value[next] <= '7') {
                        val = val * 8 + (value[next] - '0');
                        next++;
                        have++;
                    }
                }
                *out = have ? (unsigned char)val : (unsigned char)escape;
                return next;
            }
        }
    }
    *out = c;
    return i + 1;
}

int emit_string_bytes(const char *value, unsigned char *buf, int buf_cap) {
    int len;
    const char *body = emit_unquote(value, &len);
    int n = 0;
    for (int i = 0; i < len;) {
        unsigned char byte;
        i = emit_decode_byte(body, len, i, &byte);
        if (buf && n < buf_cap - 1) buf[n] = byte;
        n++;
    }
    if (buf && n < buf_cap) buf[n] = 0;
    return n + 1;
}

int emit_string_byte_count(const char *value) {
    return emit_string_bytes(value, NULL, 0);
}
