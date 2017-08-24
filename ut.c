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

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/prctl.h>
#include <sys/stat.h>

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/un.h>
#include <errno.h>
#include <dlfcn.h>
#include <unistd.h>
#include <fcntl.h>

#include "gputop-list.h"
#include "ut-utils.h"
#include "memfd.h"

#include "ut.h"
#include "ut-shared-data.h"
#include "ut-memfd-array.h"


#if defined(__i386__)
#define rmb()           __asm__ volatile("lock; addl $0,0(%%esp)" ::: "memory")
#define mb()            __asm__ volatile("lock; addl $0,0(%%esp)" ::: "memory")
#endif

#if defined(__x86_64__)
#define rmb()           __asm__ volatile("lfence" ::: "memory")
#define mb()            __asm__ volatile("mfence" ::: "memory")
#endif



#if 0
task based timing

a task:
- is tied to a thread
- begins and ends in the same stack frame


per thread state:
- task stack (we want to draw flame graphs in the end)
- 

Note: consider that a task might get preemted and switch between cpus
Use tls and lock-free ring per-thread for capturing traces
Fixed 2MB ring size

Sampling involves:
#endif


struct thread_state {
    /* A stack of uint16_t task_desc indices */
    struct array stack;

    //int stack_pointer;

    /* A header page for the shared circular buffer, including
     * the number of samples currently written to the buffer
     */
    volatile struct ut_info_page *info;

    /* The shared circular buffer for sample data */
    volatile uint8_t *buf;

    /* The size of the circular buffer */
    size_t buf_size;

    /* For samples we want to to use 16bit indices to map back to
     * the task description structures...
     */
    struct array task_desc_registry;

    /* Task descriptions are shared via ancillary data records written to
     * anonymous memory, shared with the server by passing a memfd file
     * descriptor which the server can mmap.
     */
    struct ut_memfd_stack shared_ancillary;
};


static pthread_once_t init_tls_once = PTHREAD_ONCE_INIT;
static pthread_key_t tls_key;

static size_t page_size;

static struct array thread_state_index;

#define SZ_2M (2 * 1024 * 1024)
#define UT_CIRCULAR_BUFFER_SIZE SZ_2M /* XXX: must be a power of two */

#if 0
static void
thread_destroy_cb(void *data)
{
    struct thread_state *state = data;

}
#endif

#if 0
static void
sigusr_handler(int signo)
{
}
#endif

static int
connect_to_abstract_socket(const char *socket_name)
{
    int fd;
    struct sockaddr_un addr;
    socklen_t size;
    int name_size;
    int flags;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        dbg("Failed to create PF_LOCAL socket fd\n");
        return -1;
    }

    /* XXX: Android doesn't seem to support SOCK_CLOEXEC so we use
     * fcntl() instead */
    flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
        dbg("Failed to get fd flags for setting O_CLOEXEC on socket\n");
        return 1;
    }

    if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
        dbg("Failed to set O_CLOEXEC on abstract socket\n");
        return 1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    name_size = snprintf(addr.sun_path, sizeof(addr.sun_path),
                         "%c%s", '\0', socket_name);

    if (name_size > sizeof(addr.sun_path)) {
        dbg("socket path \"\\0%s\" plus null terminator"
            " exceeds %d bytes\n", socket_name, sizeof(addr.sun_path));
        close(fd);
        return -1;
    };

    size = offsetof(struct sockaddr_un, sun_path) + name_size;

    if (connect(fd, (struct sockaddr *)&addr, size) < 0) {
        const char *msg = strerror(errno);
        dbg("Failed to connect to abstract socket: %s\n", msg);
        close(fd);
        return -1;
    }

    return fd;
}

static void
init_tls_state(void)
{
    pthread_key_create(&tls_key, NULL);

    array_init(&thread_state_index, sizeof(void *), 20);

    page_size = sysconf(_SC_PAGE_SIZE);
}

static int
get_tid(void)
{
    return syscall(SYS_gettid);
}

static struct thread_state *
get_thread_state(void)
{
    struct thread_state *state;

    pthread_once(&init_tls_once, init_tls_state);

    state = pthread_getspecific(tls_key);
    fprintf(stderr, "getspecific %p\n", state);

    if (unlikely(!state)) {
        int conductor_fd = -1;

        fprintf(stderr, "allocate thread state\n");
        state = xmalloc0(sizeof(*state));
        array_init(&state->task_desc_registry, sizeof(void *), 50);
        array_init(&state->stack, sizeof(uint16_t), 50);
        pthread_setspecific(tls_key, state);

        state->buf_size = UT_CIRCULAR_BUFFER_SIZE;

        conductor_fd = connect_to_abstract_socket("ut-conductor");
        if (conductor_fd >= 0) {
            char thread_name[16];
            char *shm_name;

            prctl(PR_GET_NAME, &thread_name);
            asprintf(&shm_name, "ut-buffer-%s", thread_name);

            int mem_fd = memfd_create(shm_name, MFD_CLOEXEC|MFD_ALLOW_SEALING);
            if (mem_fd >= 0) {
                dbg("mapping circular buffer with size = %d\n", state->buf_size + page_size);


                uint8_t *mem = memfd_mmap(mem_fd,
                                          state->buf_size + page_size,
                                          PROT_READ|PROT_WRITE);
                {
                    struct stat sb;
                    int ret = fstat(mem_fd, &sb);
                    if (ret < 0) {
                        dbg("Failed to stat memfd file descriptor\n");
                    }
                    dbg("memfd file size according to fstat() = %d\n", (int)sb.st_size);
                }

                if (mem) {
                    state->info = (void *)mem;

                    state->info->abi_version = UT_ABI_VERSION;
                    state->info->tid = get_tid();
                    state->info->sample_size = sizeof(struct ut_sample);
                    state->info->n_samples_written = 0;

                    state->buf = mem + page_size;

                    fprintf(stderr, "passing circular buffer fd\n");
                    memfd_pass(conductor_fd, mem_fd);

                    /* Initialize after passing the circular buffer fd, since
                     * this will also pass an fd for the first ancillary data
                     * buffer
                     */
                    ut_memfd_stack_init(&state->shared_ancillary,
                                        conductor_fd,
                                        "libut ancillary data");
                } else
                    fprintf(stderr, "Failed to mmap shared circular buffer\n");
            }
        } else
            fprintf(stderr, "Failed to connect to conductor\n");

        if (!state->buf) {
            uint8_t *mem = xmalloc(state->buf_size + page_size);
            state->info = (void *)mem;
            state->buf = mem + page_size;
        }

        array_append_val(&thread_state_index, struct thread_state *, state);
        array_append_val(&state->task_desc_registry,
                         struct ut_shared_task_desc *, NULL); /* index 0 reserved */
    }

    return state;
}

static uint64_t
rdtscp(uint32_t *cpuid)
{
    uint32_t tsc_lo, tsc_hi, tsc_aux;

    __asm__ __volatile__("rdtscp;"
                         : "=a"(tsc_lo), "=d"(tsc_hi), "=c"(tsc_aux)
                         : /* no input */
                         : /* no extra clobbers */);

    *cpuid = tsc_aux;
    return  (uint64_t)tsc_lo | (((uint64_t)tsc_hi) << 32);
}

static uint64_t
read_monotonic_clock(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void
_task_sample(struct thread_state *state,
             enum ut_sample_type type,
             uint16_t task_desc_index)
{
    struct ut_sample *sample;
    uint32_t offset = state->info->n_samples_written * sizeof(struct ut_sample);
    uint32_t mask = (UT_CIRCULAR_BUFFER_SIZE - 1);
    uint32_t cpuid;

    offset &= mask;
    sample = (void *)(state->buf + offset);
    sample->type = type;
    sample->padding = 0;
    sample->task_desc_index = task_desc_index;
    sample->tsc = rdtscp(&cpuid);
    sample->cpu = cpuid & 0xff;
    //sample->stack_pointer = state->stack_pointer;
    sample->stack_pointer = state->stack.len;

    /* ensure the sample only becomes visible after the contents have landed */
    mb();
    state->info->n_samples_written++;

    /* XXX: this is designed with the assumption that the clients are stopped
     * via ptrace(PTRACE_INTERRUPT) before data is read. The memory barrier
     * only ensures that the reader can trust that the most recent sample is
     * consistent. On the other hand the reader should skip the oldest sample
     * when the buffer is full since the interrupted client might have been in
     * the middle of writing a new sample.
     */
}

static uint16_t
get_task_desc_index(struct thread_state *state, struct ut_task_desc *task_desc)
{
    if (unlikely(task_desc->idx == 0)) {
#ifdef SUPPORT_TRANSIENT_DSO_TASKS
        /* TODO: search for existing id via a name index */
#endif
        uint16_t task_desc_index = state->task_desc_registry.len;

        array_append_val(&state->task_desc_registry,
                         struct ut_task_desc *,
                         task_desc);

        task_desc->idx = task_desc_index;
        dbg_assert(task_desc->idx != 0);

        /* cope with failure to connect to server */
        if (state->shared_ancillary.current_buf.size) {
            size_t record_size = (sizeof(struct ut_ancillary_record) +
                                  sizeof(struct ut_shared_task_desc));
            volatile struct ut_ancillary_record *header =
                ut_memfd_stack_memalign(&state->shared_ancillary,
                                        record_size,
                                        8); /* alignment */
            volatile struct ut_shared_task_desc *shared_desc = (void *)(header + 1);

            strncpy((char *)shared_desc->name, task_desc->name, sizeof(shared_desc->name));
            shared_desc->idx = task_desc_index;

            header->type = UT_ANCILLARY_TASK_DESC;
            header->padding = 0;

            /* Ensure the reader only sees a complete record for a non zero header
             * type - i.e. the reader can parse the records as a NULL terminated
             * sequence of records based on the type field.
             */
            mb();
            header->size = record_size;
        }
    }

    dbg_assert(state->task_desc_registry.len <= UINT16_MAX);

    return task_desc->idx;
}

void
ut_push_task(struct ut_task_desc *task_desc)
{
    struct thread_state *state = get_thread_state();
    uint16_t task_desc_idx = get_task_desc_index(state, task_desc);

    array_append_val(&state->stack, uint16_t, task_desc_idx);
    _task_sample(state, UT_SAMPLE_TASK_PUSH, task_desc_idx);
    //state->stack_pointer++;
}

void
ut_pop_task(struct ut_task_desc *task_desc)
{
    struct thread_state *state = get_thread_state();
    uint16_t task_desc_idx = get_task_desc_index(state, task_desc);

    //dbg_assert(state->stack_pointer > 0);
    //state->stack_pointer--;
    _task_sample(state, UT_SAMPLE_TASK_POP, task_desc_idx);

    dbg_assert(array_value_at(&state->stack, uint16_t, state->stack.len - 1) == task_desc_idx);
    array_remove_fast(&state->stack, state->stack.len - 1);
}

