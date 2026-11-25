#ifndef VEC_H
#define VEC_H

#include "arena.h"
#include <stddef.h>

#define VEC_GROW(array, count, capacity, elem_type, arena)                    \
    do {                                                                     \
        if ((count) >= (capacity)) {                                         \
            size_t vec_old_cap_ = (size_t)(capacity);                        \
            (capacity) = vec_old_cap_ ? vec_old_cap_ * 2 : 8;                 \
            (array) = arena_grow((arena), (array),                            \
                                 vec_old_cap_ * sizeof(elem_type),            \
                                 (size_t)(capacity) * sizeof(elem_type));     \
        }                                                                    \
    } while (0)

#endif
