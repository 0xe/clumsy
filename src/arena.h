#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

#define ARENA_ALIGNMENT 16u

typedef struct ArenaChunk {
    struct ArenaChunk *next;
    size_t capacity;
    size_t used;
    size_t padding;
    char data[];
} ArenaChunk;

typedef struct {
    ArenaChunk *head;
    size_t chunk_size;
} Arena;

extern Arena g_arena_compile;
extern Arena g_arena_scratch;

void arena_init(Arena *a, size_t chunk_size);
void *arena_alloc(Arena *a, size_t size);
void *arena_calloc(Arena *a, size_t count, size_t size);
void *arena_grow(Arena *a, void *old, size_t old_size, size_t new_size);
char *arena_strdup(Arena *a, const char *s);
void arena_reset(Arena *a);
void arena_destroy(Arena *a);

#endif
