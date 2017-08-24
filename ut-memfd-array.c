/*
 * libut - userspace tracing library
 *
 * Copyright (C) 2018 Robert Bragg
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
 *
 *
 * An append only stack of fixed sized elements.
 *
 * Data is stored in page-aligned, fixed-sized anonymous memmory buffers which
 * are shared over a given unix domain socket. When there is no more room in
 * the head buffer then a new one is allocated and its fd is sent over the
 * socket.
 *
 * The reciever of the memfd file descriptors can mmap the buffers and parse
 * entries as a NUL terminated array, or some other IPC mechanism can be used
 * to track how much data is in each buffer.
 *
 * XXX: use a compiler/write barrier to atomicaly write a particular member of
 * each entry last which can be checked when iterating entries to know that if
 * it's set then the full entry is valid.
 */

#include "memfd.h"
#include "ut-utils.h"
#include "ut-memfd-array.h"

#define MEMFD_ARRAY_BUF_PAGE_COUNT 2

static void
_stack_alloc_buffer(struct ut_memfd_stack *stack)
{
    size_t page_size = sysconf(_SC_PAGE_SIZE);

    memset(&stack->current_buf, 0, sizeof(stack->current_buf));

    stack->current_buf.fd = memfd_create(stack->debug_name,
                                         MFD_CLOEXEC|MFD_ALLOW_SEALING);
    if (stack->current_buf.fd >= 0) {
        stack->current_buf.size = MEMFD_ARRAY_BUF_PAGE_COUNT * page_size;
        stack->current_buf.offset = 0;
        stack->current_buf.data = memfd_mmap(stack->current_buf.fd,
                                             stack->current_buf.size,
                                             PROT_READ|PROT_WRITE);
        if (!stack->current_buf.data) {
            close(stack->current_buf.fd);
            memset(&stack->current_buf, 0, sizeof(stack->current_buf));
            stack->current_buf.fd = -1;
        }

        dbg("passing ancillary data fd\n");
        memfd_pass(stack->socket_fd, stack->current_buf.fd);
    }
}

void
ut_memfd_stack_init(struct ut_memfd_stack *stack,
                    int socket_fd,
                    const char *debug_name)
{
    memset(stack, 0, sizeof(*stack));
    stack->current_buf.fd = -1;

    stack->socket_fd = socket_fd;
    stack->debug_name = strdup(debug_name);

    _stack_alloc_buffer(stack);
}

static inline size_t
_align_up(size_t base, size_t alignment)
{
    return (base + alignment - 1) & ~(alignment - 1);
}

volatile void *
ut_memfd_stack_memalign(struct ut_memfd_stack *stack,
                        size_t size,
                        size_t alignment)
{
    size_t offset = _align_up(stack->current_buf.offset, alignment);

    if ((stack->current_buf.size - offset) < size) {
        _stack_alloc_buffer(stack);
        offset = 0;
    }

    if (stack->current_buf.data) {
        volatile void *ret = stack->current_buf.bytes + offset;
        stack->current_buf.offset = offset + size;
        return ret;
    } else
        return NULL;
}

