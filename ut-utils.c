#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>

#include "ut-utils.h"

/* Since we may trace mmap; this is the real deal, to avoid recursion. */
void *
ut_mmap_real(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    static void *(*real_mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset);

    /* XXX: consider cases where apiwrapper.so isn't in use */
    if (unlikely(!real_mmap)) {
        real_mmap = dlsym(RTLD_NEXT, "mmap");

        /* In case we're not really wrapping mmap atm, so RTLD_NEXT might fail */
        if (unlikely(!real_mmap))
            real_mmap = dlsym(RTLD_DEFAULT, "mmap");
    }

    return real_mmap(addr, length, prot, flags, fd, offset);
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

