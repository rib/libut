#pragma once

#include <linux/memfd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <stdint.h>

//memfd hacks, as including fcntl or linux/fcntl doesn't appear to work out for me
#ifndef F_LINUX_SPECIFIC_BASE
#define F_LINUX_SPECIFIC_BASE 1024
#endif

#ifndef F_ADD_SEALS
#define F_ADD_SEALS (F_LINUX_SPECIFIC_BASE + 9)
#define F_GET_SEALS (F_LINUX_SPECIFIC_BASE + 10)

#define F_SEAL_SEAL     0x0001  /* prevent further seals from being set */
#define F_SEAL_SHRINK   0x0002  /* prevent file from shrinking */
#define F_SEAL_GROW     0x0004  /* prevent file from growing */
#define F_SEAL_WRITE    0x0008  /* prevent writes */
#endif

int
memfd_create(const char *name, unsigned int flags);

void
memfd_pass(int socket_fd, int fd);

uint8_t *
memfd_mmap(int mem_fd, size_t size, int prot);
