#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/mman.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>

#include "ut-utils.h"

#define UT_API_WRAPPER_DSO_NAME "libut-sysapiwrappers.so"


static void *(*dlsym_next_untraced)(const char *sym);

/* Note: we handle the possiblity that the system apis mught not currently
 * be being traced via any LD_PRELOAD
 */
static void *
_find_untraced_sym(const char *sym)
{
    if (!dlsym_next_untraced) 
        dlsym_next_untraced = dlsym(RTLD_DEFAULT, "ut_dlsym_next_untraced");
    if (dlsym_next_untraced)
        return dlsym_next_untraced(sym);

    return dlsym(RTLD_DEFAULT, sym);
}

/* The tricky thing is that we don't know what the position of libut is
 * relative to our wrapper DSO, so we can't just rely on RTLD_NEXT from here
 * to be sure that we'll skip past our wrappers.
 *
 * Instead we jump through a dlsym wrapper in libut-sysapiwrappers.so so
 * we can lookup with RTLD_NEXT relative to the wrappers.
 */
#define FIND_UNTRACED_SYM(sym) \
    do { \
        if (unlikely(!real_##sym)) \
            real_##sym = _find_untraced_sym(#sym); \
    } while(0)

void *
ut_untraced_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    static void *(*real_mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset);

    FIND_UNTRACED_SYM(mmap);

    return real_mmap(addr, length, prot, flags, fd, offset);
}

int
ut_untraced_open(const char *pathname, int flags, mode_t mode)
{
    static int (*real_open)(const char *pathname, int flags, mode_t mode);

    FIND_UNTRACED_SYM(open);

    return real_open(pathname, flags, mode);
}

ssize_t
ut_untraced_read(int fd, void *buf, size_t count)
{
    static ssize_t (*real_read)(int fd, void *buf, size_t count);

    FIND_UNTRACED_SYM(read);

    return real_read(fd, buf, count);
}

void *
ut_untraced_malloc(size_t size)
{
    static void *(*real_malloc)(size_t size);

    FIND_UNTRACED_SYM(malloc);

    return real_malloc(size);
}

void *
ut_untraced_realloc(void * ptr, size_t size)
{
    static void *(*real_realloc)(void * ptr, size_t size);

    FIND_UNTRACED_SYM(realloc);

    return real_realloc(ptr, size);
}

void
ut_untraced_free(void * ptr)
{
    static void (*real_free)(void * ptr);

    FIND_UNTRACED_SYM(free);

    return real_free(ptr);
}

ssize_t
ut_untraced_sendmsg(int sockfd, const void * msg, int flags)
{
    static ssize_t (*real_sendmsg)(int sockfd, const void * msg, int flags);

    FIND_UNTRACED_SYM(sendmsg);

    return real_sendmsg(sockfd, msg, flags);
}

ssize_t
ut_untraced_recvmsg(int socket, void * msg, int flags)
{
    static ssize_t (*real_recvmsg)(int socket, void * msg, int flags);

    FIND_UNTRACED_SYM(recvmsg);

    return real_recvmsg(socket, msg, flags);
}

void
ut_send_fd(int socket_fd, int fd)
{
    struct iovec dummy_io = { .iov_base = "hi", .iov_len = 2 } ;
    union {
        struct cmsghdr align; /* ensure buf is aligned */
        char buf[CMSG_SPACE(sizeof(fd))];
    } u = {};
    struct msghdr msg = {
        .msg_iov = &dummy_io,
        .msg_iovlen = 1,
        .msg_control = u.buf,
        .msg_controllen = sizeof(u.buf) /* initially for CMSG_FIRSTHDR() to work,
                                           updated later */
    };
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    int *fd_ptr;

    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(fd));

    fd_ptr = (int *)CMSG_DATA(cmsg);
    *fd_ptr = fd;

    msg.msg_controllen = cmsg->cmsg_len;

    if (ut_untraced_sendmsg(socket_fd, &msg, 0) < 0)
        dbg("Failed to pass file descriptor: %m\n");
    else
        dbg("Passed file descriptor: %d\n", fd);
}

uint8_t *
ut_mmap_memfd_fd(int mem_fd, size_t size, int prot)
{
    ftruncate(mem_fd, size);
#ifndef ENABLE_VALGRIND_MEMFD_WORKAROUND
    fcntl(mem_fd, F_ADD_SEALS, F_SEAL_SHRINK|F_SEAL_GROW|F_SEAL_SEAL);
#endif

    dbg("mmap...\n");
    return ut_untraced_mmap(NULL, size, prot, MAP_SHARED, mem_fd, 0);
}

bool
ut_get_bool_env(const char *var)
{
    char *val = getenv(var);

    if (!val)
        return false;

    if (strcmp(val, "1") == 0 ||
        strcasecmp(val, "yes") == 0 ||
        strcasecmp(val, "on") == 0 ||
        strcasecmp(val, "true") == 0)
        return true;
    else if (strcmp(val, "0") == 0 ||
             strcasecmp(val, "no") == 0 ||
             strcasecmp(val, "off") == 0 ||
             strcasecmp(val, "false") == 0)
        return false;

    fprintf(stderr, "unrecognised value for boolean variable %s\n", var);
    return false;
}

int
ut_read_file(const char *filename, void *buf, int max)
{
    int fd;
    int n;

    memset(buf, 0, max);

    while ((fd = open(filename, 0)) < 0 && errno == EINTR)
        ;
    if (fd < 0)
        return false;

    while ((n = read(fd, buf, max - 1)) < 0 && errno == EINTR)
        ;
    close(fd);
    if (n < 0)
        return 0;

    return n;
}

bool
ut_read_file_string(const char *filename, char *buf, int buf_len)
{
    int fd;
    int n;

    memset(buf, 0, buf_len);

    while ((fd = open(filename, 0)) < 0 && errno == EINTR)
        ;
    if (fd < 0)
        return false;

    while ((n = read(fd, buf, buf_len - 1)) < 0 && errno == EINTR)
        ;
    close(fd);
    if (n <= 0)
        return false;

    buf[n - 1] = '\0';
    return true;
}

uint64_t
ut_read_file_uint64(const char *filename)
{
    char buf[32];
    int fd, n;

    while ((fd = open(filename, 0)) < 0 && errno == EINTR)
        ;
    if (fd < 0)
        return 0;

    while ((n = read(fd, buf, sizeof(buf) - 1)) < 0 && errno == EINTR)
        ;
    close(fd);
    if (n < 0)
        return 0;

    buf[n] = '\0';
    return strtoull(buf, 0, 0);
}

