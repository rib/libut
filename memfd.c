#define _GNU_SOURCE
#include <sys/syscall.h>
#include <unistd.h>

#include "memfd.h"

int
memfd_create(const char *name, unsigned int flags)
{
    return syscall(__NR_memfd_create, name, flags);
}
