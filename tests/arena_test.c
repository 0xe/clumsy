#include "arena.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "arena_test: %s:%d: %s failed\n", __FILE__,        \
                    __LINE__, #cond);                                          \
            exit(1);                                                           \
        }                                                                      \
    } while (0)

int main(void) {
    Arena a;
    arena_init(&a, 64);

    uintptr_t p1 = (uintptr_t)arena_alloc(&a, 8);
    uintptr_t p2 = (uintptr_t)arena_alloc(&a, 24);
    CHECK(p1 % ARENA_ALIGNMENT == 0);
    CHECK(p2 % ARENA_ALIGNMENT == 0);
    CHECK(p2 - p1 >= 16);

    char *s = arena_strdup(&a, "hello");
    CHECK(strcmp(s, "hello") == 0);
    CHECK(arena_strdup(&a, NULL) == NULL);

    int *nums = arena_calloc(&a, 8, sizeof(int));
    for (int i = 0; i < 8; i++) CHECK(nums[i] == 0);

    int *grown = arena_grow(&a, nums, 8 * sizeof(int), 32 * sizeof(int));
    CHECK(grown != nums);
    CHECK(grown[0] == 0);
    grown[31] = 7;
    CHECK(grown[31] == 7);

    void *large = arena_alloc(&a, 4096);
    CHECK((uintptr_t)large % ARENA_ALIGNMENT == 0);
    memset(large, 0xAB, 4096);

    arena_reset(&a);
    char *again = arena_strdup(&a, "reset");
    CHECK(strcmp(again, "reset") == 0);
    CHECK(strcmp(s, "hello") == 0);

    arena_destroy(&a);
    CHECK(a.head == NULL);
    return 0;
}
