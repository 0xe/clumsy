#ifndef HASHMAP_H
#define HASHMAP_H

#include "arena.h"
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char **keys;
    int *values;
    unsigned char *states;
    size_t count;
    size_t capacity;
    size_t used;
    Arena *arena;
} StringIntMap;

void str_int_map_init(StringIntMap *map, Arena *arena, size_t capacity);
int *str_int_map_get(StringIntMap *map, const char *key);
void str_int_map_put(StringIntMap *map, const char *key, int value);
bool str_int_map_remove(StringIntMap *map, const char *key);
void str_int_map_clear(StringIntMap *map);

#endif
