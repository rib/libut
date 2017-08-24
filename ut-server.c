#define _GNU_SOURCE
#include <sys/un.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/signalfd.h>

#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

#include <uv.h>

#include "ut-utils.h"
#include "ut-shared-data.h"
#include "memfd.h"

#ifdef DEBUG
#include <assert.h>
#define dbg_assert assert
#define dbg(format, ...) fprintf(stderr, format, ##__VA_ARGS__)
#else
#define dbg_assert(x) do {} while (0 && (x))
#define dbg(format, ...) do { } while(0)
#endif

struct ut_client {
    int fd;
    uint32_t tid;
    volatile struct ut_info_page *info;
    volatile struct ut_sample *buf;

    uv_poll_t poll;
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

    if (recvmsg(socket_fd, &msg, 0) < 0)
        dbg("Failed to receive file descriptor: %m\n");

    dbg("received message\n");

    cmsg = CMSG_FIRSTHDR(&msg);
    if (cmsg->cmsg_type == SCM_RIGHTS) {
        struct stat sb;
        uint8_t *buf;

        fd_ptr = (int *)CMSG_DATA(cmsg);
        fd = *fd_ptr;
        dbg("> received fd = %d\n", fd);
    } else
        dbg("Expected SCM_RIGHTS cmsg with memfd file descriptor\n");

    return fd;
}

static void
client_fd_cb(uv_poll_t *handle, int status, int events)
{
    struct ut_client *client = handle->data;
    int ancillary_data_fd;

    fprintf(stderr, "client_fd_cb\n");

    ancillary_data_fd = receive_fd(client->fd);

    dbg("received ancillary data fd for client = %d\n", client->tid);
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

    client->tid = client->info->tid;
    dbg("client thread id = %d\n", client->tid);

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

static void
capture_data(void)
{
    struct ut_client *stopped_clients[all_clients.len];
    int n_stopped_clients;

    for (int i = 0; i < all_clients.len; i++) {
        struct ut_client *client = array_value_at(&all_clients, struct ut_client *, i);
        int ret;

        dbg("interupting client = %d\n", client->tid);

        /* PTRACE_SEIZE + _INTERRUPT gives as a no-side-effect way of stopping
         * the threads we're interested in, and on the offchance that we
         * crash the kernel will automatically resume running the interrupted
         * threads too.
         */
        ret = ptrace(PTRACE_SEIZE, client->tid, 0, 0);
        if (ret < 0) {
            fprintf(stderr, "ptrace failed to seize tid = %d: %m\n", (int)client->tid);
            continue;
        }

        ret = ptrace(PTRACE_INTERRUPT, client->tid, 0, 0);
        if (ret < 0) {
            fprintf(stderr, "ptrace failed to interrupt tid: %m\n", (int)client->tid);
            continue;
        }

        ret = waitid(P_PID, client->tid, NULL, WSTOPPED);
        if (ret < 0) {
            fprintf(stderr, "failed to wait for thread %d to stop: %m\n", (int)client->tid);
            continue;
        }
        stopped_clients[n_stopped_clients++] = client;
    }

    if (!n_stopped_clients) {
        fprintf(stderr, "Failed to stop any clients to collect metrics\n");
        return;
    }

    dbg("All clients stopped; ready to read data\n");

    for (int i = 0; i < n_stopped_clients; i++) {
        struct ut_client *client = array_value_at(&all_clients, struct ut_client *, i);

        dbg("client n_samples = %d\n", client->info->n_samples_written);
    }
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

    printf("%d listening for clients\n", (int)getpid());
    uv_run(loop, 0);
}
