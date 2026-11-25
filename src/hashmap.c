#include "hashmap.h"
#include <limits.h>
#include <string.h>

#define MAP_EMPTY 0u
#define MAP_FULL 1u
#define MAP_DELETED 2u

static size_t map_hash(const char *key) {
    size_t h = (size_t)1469598103934665603ULL;
    for (const unsigned char *p = (const unsigned char *)key; *p; p++) {
        h ^= *p;
        h *= (size_t)1099511628211ULL;
    }
    return h;
}

static size_t map_capacity(size_t requested) {
    size_t cap = 16;
    while (cap < requested) cap *= 2;
    return cap;
}

static void map_alloc(StringIntMap *map, size_t capacity) {
    map->keys = arena_calloc(map->arena, capacity, sizeof(*map->keys));
    map->values = arena_calloc(map->arena, capacity, sizeof(*map->values));
    map->states = arena_calloc(map->arena, capacity, sizeof(*map->states));
    map->capacity = capacity;
    map->used = 0;
}

static void map_insert(StringIntMap *map, const char *key, int value) {
    size_t mask = map->capacity - 1;
    size_t slot = map_hash(key) & mask;
    while (map->states[slot] == MAP_FULL) slot = (slot + 1) & mask;
    if (map->states[slot] == MAP_EMPTY) map->used++;
    map->keys[slot] = key;
    map->values[slot] = value;
    map->states[slot] = MAP_FULL;
}

static void map_grow(StringIntMap *map) {
    const char **old_keys = map->keys;
    int *old_values = map->values;
    unsigned char *old_states = map->states;
    size_t old_capacity = map->capacity;

    map_alloc(map, old_capacity * 2);
    for (size_t i = 0; i < old_capacity; i++) {
        if (old_states[i] == MAP_FULL) map_insert(map, old_keys[i], old_values[i]);
    }
}

void str_int_map_init(StringIntMap *map, Arena *arena, size_t capacity) {
    map->count = 0;
    map->arena = arena;
    map_alloc(map, map_capacity(capacity));
}

int *str_int_map_get(StringIntMap *map, const char *key) {
    size_t mask = map->capacity - 1;
    size_t slot = map_hash(key) & mask;
    while (map->states[slot] != MAP_EMPTY) {
        if (map->states[slot] == MAP_FULL && strcmp(map->keys[slot], key) == 0) {
            return &map->values[slot];
        }
        slot = (slot + 1) & mask;
    }
    return NULL;
}

void str_int_map_put(StringIntMap *map, const char *key, int value) {
    int *existing = str_int_map_get(map, key);
    if (existing) {
        *existing = value;
        return;
    }
    if ((map->used + 1) * 10 >= map->capacity * 7) map_grow(map);
    map_insert(map, key, value);
    map->count++;
}

bool str_int_map_remove(StringIntMap *map, const char *key) {
    int *value = str_int_map_get(map, key);
    if (!value) return false;
    size_t slot = (size_t)(value - map->values);
    map->states[slot] = MAP_DELETED;
    map->keys[slot] = NULL;
    map->count--;
    return true;
}

void str_int_map_clear(StringIntMap *map) {
    for (size_t i = 0; i < map->capacity; i++) {
        map->keys[i] = NULL;
        map->values[i] = 0;
        map->states[i] = MAP_EMPTY;
    }
    map->count = 0;
    map->used = 0;
}
