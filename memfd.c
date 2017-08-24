#include "ut-utils.h"

#include <sys/syscall.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "ut-utils.h"
#include "memfd.h"

int
memfd_create(const char *name, unsigned int flags)
{
    return syscall(__NR_memfd_create, name, flags);
}

void
memfd_pass(int socket_fd, int fd)
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

    if (sendmsg(socket_fd, &msg, 0) < 0)
        dbg("Failed to pass file descriptor: %m\n");
    else
        dbg("Passed file descriptor: %d\n", fd);
}

uint8_t *
memfd_mmap(int mem_fd, size_t size, int prot)
{
    ftruncate(mem_fd, size);
    fcntl(mem_fd, F_ADD_SEALS, F_SEAL_SHRINK|F_SEAL_GROW|F_SEAL_SEAL);

    dbg("mmap...\n");
    return ut_mmap_real(NULL, size, prot, MAP_SHARED, mem_fd, 0);
}
