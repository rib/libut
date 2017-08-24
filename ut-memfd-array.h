#pragma once


struct ut_memfd_stack
{
    /* private */

    int socket_fd; /* Each new buffer allocated gets forwarded over this socket */
    char *debug_name;

    /* Only track one, head buffer. */
    struct {
        int fd;
        size_t size;
        off_t offset;
        union {
            volatile void *data;
            volatile uint8_t *bytes;
        };
    } current_buf;
};



void
ut_memfd_stack_init(struct ut_memfd_stack *stack,
                    int socket_fd,
                    const char *debug_name);

volatile void *
ut_memfd_stack_memalign(struct ut_memfd_stack *stack,
                        size_t size,
                        size_t alignment);

