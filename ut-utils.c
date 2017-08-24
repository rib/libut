#define _GNU_SOURCE
#include <dlfcn.h>

#include "ut-utils.h"

/* Since we may trace mmap; this is the real deal, to avoid recursion. */
void *
ut_mmap_real(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    static void *(*real_mmap)(void *addr, size_t length, int prot, int flags, int fd, off_t offset);

    if (unlikely(!real_mmap))
        real_mmap = dlsym(RTLD_NEXT, "mmap");

    return real_mmap(addr, length, prot, flags, fd, offset);
}



