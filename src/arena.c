#include "arena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Arena g_arena_compile;
Arena g_arena_scratch;

static size_t arena_align(size_t size) {
    return (size + ARENA_ALIGNMENT - 1) & ~(ARENA_ALIGNMENT - 1);
}

void arena_init(Arena *a, size_t chunk_size) {
    a->head = NULL;
    a->chunk_size = arena_align(chunk_size ? chunk_size : ARENA_ALIGNMENT);
}

void *arena_alloc(Arena *a, size_t size) {
    size = arena_align(size);
    if (size == 0) size = ARENA_ALIGNMENT;

    if (!a->head || a->head->capacity - a->head->used < size) {
        size_t capacity = a->chunk_size;
        if (capacity < size) capacity = size;
        ArenaChunk *chunk = malloc(sizeof(ArenaChunk) + capacity);
        if (!chunk) {
            fprintf(stderr, "Error: arena allocation failed\n");
            exit(1);
        }
        chunk->next = a->head;
        chunk->capacity = capacity;
        chunk->used = 0;
        a->head = chunk;
    }

    void *ptr = a->head->data + a->head->used;
    a->head->used += size;
    return ptr;
}

void *arena_calloc(Arena *a, size_t count, size_t size) {
    size_t total = count * size;
    void *ptr = arena_alloc(a, total);
    memset(ptr, 0, total);
    return ptr;
}

void *arena_grow(Arena *a, void *old, size_t old_size, size_t new_size) {
    void *ptr = arena_alloc(a, new_size);
    if (old) memcpy(ptr, old, old_size < new_size ? old_size : new_size);
    return ptr;
}

char *arena_strdup(Arena *a, const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *dup = arena_alloc(a, len);
    memcpy(dup, s, len);
    return dup;
}

void arena_reset(Arena *a) {
    for (ArenaChunk *chunk = a->head; chunk; chunk = chunk->next) {
        chunk->used = 0;
    }
}

void arena_destroy(Arena *a) {
    ArenaChunk *chunk = a->head;
    while (chunk) {
        ArenaChunk *next = chunk->next;
        free(chunk);
        chunk = next;
    }
    a->head = NULL;
}
