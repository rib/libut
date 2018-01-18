/*
 * libut - Userspace Tracing Toolkit
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
 */


#define _GNU_SOURCE
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <json.h>

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#include <uv.h>

#include "ut-utils.h"
#include "ut-shared-data.h"
#include "gputop-list.h"
#include "memfd.h"

#ifdef DEBUG
#include <assert.h>
#define dbg_assert assert
#define dbg(format, ...) fprintf(stderr, format, ##__VA_ARGS__)
#else
#define dbg_assert(x) do {} while (0 && (x))
#define dbg(format, ...) do { } while(0)
#endif

struct ut_ancillary_buffer {
    gputop_list_t link;
    int fd;
    uint8_t *buf;
    uint32_t buf_size;
};

struct ut_client {
    int fd;
    volatile struct ut_info_page *info;
    volatile struct ut_sample *buf;
    uint32_t buf_size;

    gputop_list_t ancillary_buffers;
    struct array task_descriptors;

    uv_poll_t poll;

    bool exited;

    char process_name[64];
    char thread_name[64];
};

static struct array all_clients;

static int listener_fd;
static uv_poll_t listener_poll;

static int signal_poll_fd;
static uv_poll_t signal_poll;


int
listen_on_abstract_socket(const char *name)
{
    struct sockaddr_un addr;
    socklen_t size, name_size;
    int fd;
    int flags;

    fd = socket(PF_LOCAL, SOCK_STREAM, 0);
    if (fd < 0) {
        dbg("Failed to create socket for listening: %s",
            strerror(errno));
        return false;
    }

    /* XXX: Android doesn't seem to support SOCK_CLOEXEC so we use
     * fcntl() instead */
    flags = fcntl(fd, F_GETFD);
    if (flags == -1) {
        dbg("Failed to get fd flags for setting O_CLOEXEC: %s\n",
            strerror(errno));
        return false;
    }

    if (fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) {
        dbg("Failed to set O_CLOEXEC on abstract socket: %s\n",
            strerror(errno));
        return false;
    }

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    name_size =
        snprintf(addr.sun_path, sizeof addr.sun_path, "%c%s", '\0', name);
    size = offsetof(struct sockaddr_un, sun_path) + name_size;
    if (bind(fd, (struct sockaddr *)&addr, size) < 0) {
        dbg("failed to bind to @%s: %s\n", addr.sun_path + 1, strerror(errno));
        close(fd);
        return false;
    } 

    if (listen(fd, 1) < 0) {
        dbg("Failed to start listening on socket: %s\n", strerror(errno));
        close(fd);
        return false;
    }

    return fd;
}

static int
receive_fd(int socket_fd)
{
    char dummy_io_buf[8];
    struct iovec dummy_io = { .iov_base = dummy_io_buf, .iov_len = sizeof(dummy_io_buf) };
    union {
        struct cmsghdr align; /* ensure buf is aligned */
        char buf[CMSG_SPACE(sizeof(int))];
    } u = {};
    struct msghdr msg = {
        .msg_iov = &dummy_io,
        .msg_iovlen = 1,
        .msg_control = u.buf,
        .msg_controllen = sizeof(u.buf)
    };
    struct cmsghdr *cmsg;
    int *fd_ptr;
    int fd = -1;
    int ret;

    while ((ret = recvmsg(socket_fd, &msg, MSG_NOSIGNAL)) < 0 && errno == EINTR)
        ;

    if (ret < 0) {
        dbg("Failed to receive message: %m\n");
        return -1;
    }

    dbg("received message\n");

    cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg && cmsg->cmsg_type == SCM_RIGHTS) {
        struct stat sb;
        uint8_t *buf;

        fd_ptr = (int *)CMSG_DATA(cmsg);
        fd = *fd_ptr;
        dbg("> received fd = %d\n", fd);
    } else {
        uint8_t *buf;
        dbg("Expected SCM_RIGHTS cmsg with memfd file descriptor\n");
        dbg("> msg.msg_iovlen = %d\n", (int)msg.msg_iovlen);
        buf = msg.msg_iov->iov_base;
        buf[msg.msg_iov->iov_len - 1] = '\0';
        dbg("> msg.msg_iov->iov_len = %d;\n", (int)msg.msg_iov->iov_len);
        dbg("> msg.msg_iov->iov_base = %p = \"%s\";\n", buf, buf);
        dbg("> msg.msg_controllen = %d\n", (int)msg.msg_controllen);
        dbg("> msg.msg_flags:\n");
        if (msg.msg_flags & MSG_EOR)
            dbg(">   MSG_EOR\n");
        if (msg.msg_flags & MSG_OOB)
            dbg(">   MSG_OOB\n");
        if (msg.msg_flags & MSG_TRUNC)
            dbg(">   MSG_TRUNC\n");
        if (msg.msg_flags & MSG_CTRUNC)
            dbg(">   MSG_CTRUNC\n");
        dbg("> cmsg = %p\n", cmsg);
        if (cmsg) {
            dbg("> cmsg.cmsg_type = %d\n", (int)cmsg->cmsg_type);
        }
    }

    return fd;
}

static void
sever_client(struct ut_client *client)
{
    dbg("severing client fd = %d\n", client->fd);
    uv_poll_stop(&client->poll);
    close(client->fd);
    client->exited = true;
}

static bool
update_client_names(struct ut_client *client)
{
    char filename[64] = { '\0' };
    char process_name[64] = { '\0' };
    char thread_name[64] = { '\0' };

    snprintf(filename, sizeof(filename), "/proc/%d/comm", client->info->pid);
    if (!ut_read_file_string(filename, process_name, sizeof(process_name))) {
        fprintf(stderr, "> failed to update process name (already exited)\n");
        return false;
    }

    strncpy(client->process_name, process_name, sizeof(client->process_name));

    snprintf(filename, sizeof(filename), "/proc/%d/task/%d/comm",
             client->info->pid, client->info->tid);

    if (!ut_read_file_string(filename, thread_name, sizeof(thread_name))) {
        fprintf(stderr, "> failed to read thread name (already exited)\n");
        return false;
    }

    strncpy(client->thread_name, thread_name, sizeof(client->thread_name));

    return true;
}

static void
client_fd_cb(uv_poll_t *handle, int status, int events)
{
    struct ut_client *client = handle->data;
    struct ut_ancillary_buffer *ancillary;
    int ancillary_data_fd;
    struct stat sb;
    void *buf;

    fprintf(stderr, "client_fd_cb: client=%p\n", client);
    if (!client->info) {
        fprintf(stderr, "> Spurious client->info == NULL!\n");
        sever_client(client);
        return;
    }
    fprintf(stderr, "> client thread id = %d\n", client->info->tid);

    if (!update_client_names(client)) {
        sever_client(client);
        return;
    }
    fprintf(stderr, "> client thread name = \"%s\"\n", client->thread_name);

    ancillary_data_fd = receive_fd(client->fd);
    if (ancillary_data_fd < 0) {
        sever_client(client);
        return;
    }

    fstat(ancillary_data_fd, &sb);
    dbg("ancillary buffer size = %d\n", (int)sb.st_size);

    ancillary = xmalloc0(sizeof(*ancillary));
    ancillary->fd = ancillary_data_fd;

    buf = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, ancillary_data_fd, 0);
    if (!buf) {
        fprintf(stderr, "Failed to mmap client's ancillary data buffer: %m\n");
        free(ancillary);
        close(ancillary_data_fd);

        sever_client(client);
        return;
    }

    ancillary->buf = (void *)buf;
    ancillary->buf_size = sb.st_size;

    dbg("received ancillary data fd for client = %p/tid=%d, size = %d bytes\n",
        client, client->info->tid, ancillary->buf_size);

    gputop_list_insert(client->ancillary_buffers.prev, &ancillary->link);
}

static void
connect_new_client(void)
{
    struct sockaddr_un addr;
    socklen_t len;
    int client_fd = -1;
    int circular_buf_fd;
    uint8_t *buf;
    struct stat sb;
    struct ut_info_page *info;
    struct ut_client *client;
    size_t page_size = sysconf(_SC_PAGE_SIZE);
    uv_loop_t *loop = uv_default_loop();

    fprintf(stderr, "New client\n");

    client_fd = accept4(listener_fd, (struct sockaddr *)&addr, &len,
                        SOCK_CLOEXEC);

    fprintf(stderr, "Connected\n");

    circular_buf_fd = receive_fd(client_fd);

    if (circular_buf_fd < 0) {
        fprintf(stderr, "Failed to fetch fd for circular buffer from client\n");
        close(client_fd);
        return;
    }

    fstat(circular_buf_fd, &sb);
    dbg("circular buffer size = %d\n", (int)sb.st_size);

    client = xmalloc0(sizeof(*client));
    client->fd = client_fd;

    buf = mmap(NULL, sb.st_size, PROT_READ, MAP_SHARED, circular_buf_fd, 0);
    if (!buf) {
        fprintf(stderr, "Failed to mmap client's circular buffer of samples: %m\n");
        free(client);
        close(circular_buf_fd);
        close(client_fd);
        return;
    }

    client->info = (void *)buf;
    client->buf = (void *)(buf + page_size);
    client->buf_size = sb.st_size - page_size;

    dbg("client thread id = %d\n", client->info->tid);

    gputop_list_init(&client->ancillary_buffers);

    client->poll.data = client;
    uv_poll_init(loop, &client->poll, client_fd);
    uv_poll_start(&client->poll, UV_READABLE, client_fd_cb);

    array_append_val(&all_clients, struct ut_client *, client);
}

static void
listener_cb(uv_poll_t *handle, int status, int events)
{
    fprintf(stderr, "listener_cb\n");
    if (events & UV_READABLE)
        connect_new_client();
}

int
sort_clients_cb(const void *v0, const void *v1)
{
    const struct ut_client *c0 = *(struct ut_client **)v0;
    const struct ut_client *c1 = *(struct ut_client **)v1;

    if (c0->info->pid == c1->info->pid)
        return c1->info->tid - c0->info->tid;
    else
        return c1->info->pid - c0->info->pid;
}

static void
_js_client_append_ancillary_data(JsonNode *js_client, struct ut_client *client)
{
    JsonNode *js_ancillary = json_mkarray();
    struct ut_ancillary_buffer *ancillary;
    int task_desc_index = 1; /* index 0 reserved */

    gputop_list_for_each(ancillary, &client->ancillary_buffers, link) {
        for (int i = 0; i < ancillary->buf_size; ) {
            struct ut_ancillary_record *header = (void *)(ancillary->buf + i);

            if (!header->type)
                break;

            switch (header->type) {
                case UT_ANCILLARY_TASK_DESC: {
                    struct ut_shared_task_desc *desc = (void *)(header + 1);
                    JsonNode *js_record = json_mkobject();
                    JsonNode *js_record_type = json_mkstring("task-desc");
                    JsonNode *js_task_name = json_mkstring(desc->name);
                    JsonNode *js_task_id = json_mknumber(task_desc_index++);

                    json_append_member(js_record, "type", js_record_type);
                    json_append_member(js_record, "name", js_task_name);
                    json_append_member(js_record, "index", js_task_id);
                    json_append_element(js_ancillary, js_record);
                    break;
                }
            }

            i += header->size;
        }
    }

    json_append_member(js_client, "ancillary", js_ancillary);
}

static void
_js_client_append_samples(JsonNode *js_client,
                          struct ut_client *client,
                          uint64_t *epoch)
{
    uint32_t n_samples = client->info->n_samples_written;
    uint32_t max_samples = client->buf_size / client->info->sample_size;
    struct ut_sample *samples = (void *)client->buf;
    JsonNode *js_samples;

    uint32_t tail;

    if (n_samples >= max_samples) {
        /* XXX: +1 to skip the oldest sample which client might be in
         * the middle of overwriting... */
        tail = n_samples - max_samples + 1;
        tail %= max_samples;
        n_samples %= max_samples;
    } else
        tail = 0;

    js_samples = json_mkarray();

    for (int i = 0; i < n_samples; i++) {
        uint32_t pos = (tail + i) % max_samples;
        struct ut_sample *sample = &samples[pos];
        JsonNode *js_sample, *js_type, *js_cpu, *js_stack_depth, *js_task;
        JsonNode *js_timestamp;
        uint64_t progress_ns;
        double progress_sec;

        if (*epoch == 0)
            *epoch = sample->timestamp;

        if (sample->timestamp < *epoch)
            continue;

        js_sample = json_mkobject();

        progress_ns = sample->timestamp - *epoch;
        progress_sec = (double)progress_ns / 1000000000.0;

        js_type = json_mknumber(sample->type);
        js_timestamp = json_mknumber(progress_sec);
        js_cpu = json_mknumber(sample->cpu);
        js_stack_depth = json_mknumber(sample->stack_pointer);
        js_task = json_mknumber(sample->task_desc_index);

        json_append_member(js_sample, "type", js_type);
        json_append_member(js_sample, "timestamp", js_timestamp);
        json_append_member(js_sample, "cpu", js_cpu);
        json_append_member(js_sample, "stack_depth", js_stack_depth);
        json_append_member(js_sample, "task", js_task);

        json_append_element(js_samples, js_sample);
    }

    json_append_member(js_client, "samples", js_samples);
}

static void
capture_data(void)
{
    struct ut_client *stopped_clients[all_clients.len];
    int n_stopped_clients = 0;
    JsonNode *top;
    uint64_t epoch = 0;
    struct ancillary_buffer *ancillary;

    for (int i = 0; i < all_clients.len; i++) {
        struct ut_client *client = array_value_at(&all_clients, struct ut_client *, i);
        int ret;
        int err = 0;

        dbg("capturing data for client = %p\n", client);
        if (!client->info) {
            dbg("> Spurious client->info == NULL\n");
            continue;
        }

        dbg("> client thread id = %d\n", client->info->tid);

        update_client_names(client);
        dbg("> client thread name = \"%s\"\n", client->thread_name);

        /* PTRACE_SEIZE + _INTERRUPT gives as a no-side-effect way of stopping
         * the threads we're interested in, and on the offchance that we
         * crash the kernel will automatically resume running the interrupted
         * threads too.
         */
        err = 0;
        ret = ptrace(PTRACE_SEIZE, client->info->tid, 0, 0);
        if (ret < 0) {
            err = errno;
            if (err == ESRCH) {
                dbg("PTRACE_SEIZE failed for exited thread\n");
                client->exited = true;
            } else {
                fprintf(stderr, "ptrace failed to seize tid = %d: %m\n",
                        (int)client->info->tid);
                continue;
            }
        }

        if (!client->exited) {
            ret = ptrace(PTRACE_INTERRUPT, client->info->tid, 0, 0);
            if (ret < 0) {
                fprintf(stderr, "ptrace failed to interrupt tid: %m\n",
                        (int)client->info->tid);
                continue;
            }

            ret = waitid(P_PID, client->info->tid, NULL, WSTOPPED);
            if (ret < 0) {
                fprintf(stderr, "failed to wait for thread %d to stop: %m\n",
                        (int)client->info->tid);
                continue;
            }
        }

        stopped_clients[n_stopped_clients++] = client;
    }

    if (!n_stopped_clients) {
        fprintf(stderr, "Failed to stop any clients to collect metrics\n");
        return;
    }

    dbg("All clients stopped; ready to read data\n");

    qsort(stopped_clients, n_stopped_clients, sizeof(void *),
          sort_clients_cb);

    top = json_mkarray();

    for (int i = 0; i < n_stopped_clients; i++) {
        struct ut_client *client = stopped_clients[i];
        JsonNode *js_client = json_mkobject();
        JsonNode *js_process_name, *js_thread_name;
        JsonNode *js_client_type = json_mkstring("thread");

        json_append_member(js_client, "type", js_client_type);

        js_process_name = json_mkstring(client->process_name);
        json_append_member(js_client, "name", js_process_name);

        js_thread_name = json_mkstring(client->thread_name);
        json_append_member(js_client, "thread_name", js_thread_name);

        dbg("client %s:%s n_samples = %d\n",
            client->process_name,
            client->thread_name,
            client->info->n_samples_written);

        _js_client_append_ancillary_data(js_client, client);
        _js_client_append_samples(js_client, client, &epoch);
        json_append_element(top, js_client);
    }

    fprintf(stdout, "%s", json_encode(top));
    json_delete(top);

    /* don't explicitly detach from ptrace, since we're about to exit anyway */
}

static void
signal_cb(uv_poll_t *handle, int status, int events)
{
    fprintf(stderr, "Dumping data\n");
    capture_data();
    exit(0);
}

int
main(int argc, char **argv)
{
    uv_loop_t *loop = uv_default_loop();
    sigset_t mask;

    array_init(&all_clients, sizeof(void *), 128);

    listener_fd = listen_on_abstract_socket("ut-conductor");

    uv_poll_init(loop, &listener_poll, listener_fd);
    uv_poll_start(&listener_poll, UV_READABLE, listener_cb);

    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGQUIT);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    signal_poll_fd = signalfd(-1, &mask, SFD_CLOEXEC);
    if (signal_poll_fd < 0) {
        fprintf(stderr, "Failed to create signal fd: %m\n");
        exit(1);
    }

    uv_poll_init(loop, &signal_poll, signal_poll_fd);
    uv_poll_start(&signal_poll, UV_READABLE, signal_cb);

    fprintf(stderr, "%d listening for clients\n", (int)getpid());
    uv_run(loop, 0);
}
