/*
 * libut - userspace tracing library
 *
 * Copyright (C) 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#pragma once

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define unlikely(x) __builtin_expect(x, 0)

#define MIN(a, b) ({ __typeof__ (a) _a_tmp = (a); \
                   __typeof__ (b) _b_tmp = (b); \
                   _a_tmp < _b_tmp ? _a_tmp : _b_tmp; })
#define MAX(a, b) ({ __typeof__ (a) _a_tmp = (a); \
                   __typeof__ (b) _b_tmp = (b); \
                   _a_tmp > _b_tmp ? _a_tmp : _b_tmp; })

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(*(x)))


#ifdef DEBUG
#include <stdio.h>
#include <assert.h>
#define dbg_assert assert
#define dbg(format, ...) fprintf(stderr, format, ##__VA_ARGS__)
#else
#define dbg_assert(x) do {} while (0 && (x))
#define dbg(format, ...) do { } while(0)
#endif

#define UINT_TO_PTR(X) ((void *)((uintptr_t)(X)))
#define PTR_TO_UINT(X) ((uintptr_t)(X)))


static inline void *
xmalloc(size_t size)
{
    void *ret = malloc(size);
    if (!ret)
        exit(1);
    return ret;
}

static inline void *
xmalloc0(size_t size)
{
    void *ret = malloc(size);
    if (!ret)
        exit(1);
    memset(ret, 0, size);
    return ret;
}

static inline void *
xrealloc(void *ptr, size_t size)
{
    void *ret = realloc(ptr, size);
    if (!ret)
        exit(1);
    return ret;
}

struct array
{
    size_t elem_size;
    int len;

    size_t size;
    union {
        void *data;
        uint8_t *bytes;
    };
};

static inline void
array_init(struct array *array, size_t elem_size, int alloc_len)
{
    array->elem_size = elem_size;
    array->len = 0;
    array->size = elem_size * alloc_len;
    array->data = xmalloc(array->size);
}

static inline void
array_free(struct array *array)
{
    free(array->data);

    array->data = NULL;
    array->elem_size = 0;
    array->len = 0;
}

static inline void
array_set_len(struct array *array, int len)
{
    size_t needed = len * array->elem_size;

    array->len = len;

    if (array->size >= needed)
        return;

    array->size = MAX(needed, array->size * 1.7);
    array->data = xrealloc(array->data, array->size);
}

static inline void
array_remove_fast(struct array *array, int idx)
{
    uint8_t *elem;
    uint8_t *last;

    array->len--;
    if (idx == array->len)
        return;

    elem = array->bytes + idx * array->elem_size;
    last = array->bytes + array->len * array->elem_size;
    memcpy(elem, last, array->elem_size);
}

#define array_value_at(ARRAY, TYPE, IDX) ({ \
    dbg_assert(sizeof(TYPE) == (ARRAY)->elem_size); \
    dbg_assert(IDX < (ARRAY)->len); \
    *(((TYPE *)((ARRAY)->data)) + IDX); \
})

#define array_element_at(ARRAY, TYPE, IDX) ({ \
    dbg_assert(sizeof(TYPE) == (ARRAY)->elem_size); \
    dbg_assert(IDX < (ARRAY)->len); \
    (((TYPE *)((ARRAY)->data)) + IDX); \
})

/* XXX: don't simplify to using typeof since it's too easy to muddle up passing
 * a pointer to the thing you want to append instead of the value, which the
 * explicitly stated TYPE helps catch.
 */
#define array_append_val(ARRAY, TYPE, VALUE) do {  \
    int len = (ARRAY)->len; \
    dbg_assert(sizeof(TYPE) == (ARRAY)->elem_size); \
    array_set_len((ARRAY), len + 1); \
    *(((TYPE *)((ARRAY)->data)) + len) = (VALUE); \
} while(0)

#define array_append_val_at(ARRAY, TYPE, VALUE_PTR) do {  \
    int len = (ARRAY)->len; \
    TYPE *dst; \
    dbg_assert(sizeof(TYPE) == (ARRAY)->elem_size); \
    array_set_len((ARRAY), len + 1); \
    dst = (((TYPE *)((ARRAY)->data)) + len); \
    memcpy(dst, VALUE_PTR, sizeof(TYPE)); \
} while(0)


void *
ut_mmap_real(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
