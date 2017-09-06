#define _GNU_SOURCE
#include <sys/types.h>
#include <dlfcn.h>

#include <stddef.h>
#include <stdbool.h>
#include <string.h>

#include "ut.h"

/* dlsym uses calloc, so to break the recursion we need a temporary fallback */
static uint8_t tmp_calloc_heap[4096];
static size_t tmp_calloc_off;

#define unlikely(x) __builtin_expect(x, 0)

static void (*ut_push_task_ptr)(struct ut_task_desc *task_desc);
static void (*ut_pop_task_ptr)(struct ut_task_desc *task_desc);

void *ut_mmap_real(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int ut_open_real(const char *pathname, int flags, mode_t mode);
ssize_t ut_read_real(int fd, void *buf, size_t count);
//void *ut_untraced_malloc(size_t size);
//void *ut_untraced_realloc(void * ptr, size_t size);
//void ut_untraced_free(void * ptr);
ssize_t ut_untraced_sendmsg(int sockfd, const void * msg, int flags);
ssize_t ut_untraced_recvmsg(int socket, void * msg, int flags);




/* Provided so libut has a way to lookup the RTLD_NEXT
 * symbol - relative to these wrappers - to be able to
 * bypass the tracing (to avoid recursion)
 */
void *
ut_dlsym_next_untraced(const char *sym)
{
    return dlsym(RTLD_NEXT, sym);
}

/* XXX: note we aren't using a pthread_once_t but it's fine
 * have a race here since the result will be the same if
 * repeated.
 */
static void
load_libut(void)
{
    void *handle = dlopen("libut.so", RTLD_LAZY);
    if (handle) {
        ut_push_task_ptr = dlsym(handle, "ut_push_task");
        ut_pop_task_ptr = dlsym(handle, "ut_pop_task");
    }
}

/* Tricky cases to handle, like dlsym using calloc */
#if 0
void *
malloc(size_t size)
{
    static struct ut_task_desc task_desc = {
        .name = "malloc"
    };
    void * ret;

    ut_push_task(&task_desc);
    ret = ut_untraced_malloc(size);
    ut_pop_task(&task_desc);

    return ret;
}

void
free(void * ptr)
{
    static struct ut_task_desc task_desc = {
        .name = "free"
    };

    ut_push_task(&task_desc);
    ut_untraced_free(ptr);
    ut_pop_task(&task_desc);
}

void *
tmp_malloc_zeroed(size_t size)
{
    uint8_t *ret = tmp_calloc_heap + tmp_calloc_off;

    if (tmp_calloc_off >= sizeof(tmp_calloc_heap))
        return NULL;

    tmp_calloc_off = (tmp_calloc_off + size + 7) & ~0x7;

    return ret;
}

void *
calloc(size_t nmemb, size_t size)
{
    static void *(*real_calloc)(size_t nmemb, size_t size);
    static bool in_dlsym;
    static struct ut_task_desc task_desc = {
        .name = "calloc"
    };
    void * ret;

    /* XXX: calloc causes trouble becuse it's used by dlsym */
    if (unlikely(!real_calloc)) {
        if (!in_dlsym) {
            in_dlsym = true;
            real_calloc = dlsym(RTLD_NEXT, "calloc");
            in_dlsym = false;
        } else
            return tmp_malloc_zeroed(nmemb * size);
    } else {
        ut_push_task(&task_desc);
        ret = real_calloc(nmemb, size);
        ut_pop_task(&task_desc);
    }

    return ret;
}

void *
realloc(void * ptr, size_t size)
{
    static struct ut_task_desc task_desc = {
        .name = "realloc"
    };
    void * ret;

    ut_push_task(&task_desc);
    ret = ut_untraced_realloc(ptr, size);
    ut_pop_task(&task_desc);

    return ret;
}
#endif



void *
mmap(void * addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    static void *(*real_mmap)(void * addr, size_t len, int prot, int flags, int fd, off_t offset);
    static struct ut_task_desc task_desc = {
        .name = "mmap"
    };
    void * ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real_mmap))
        real_mmap = dlsym(RTLD_NEXT, "mmap");

    ut_push_task_ptr(&task_desc);
    ret = real_mmap(addr, len, prot, flags, fd, offset);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

int
open(const char * path, int flags, mode_t mode)
{
    static int(*real_open)(const char * path, int flags, mode_t mode);
    static struct ut_task_desc task_desc = {
        .name = "open"
    };
    int ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real_open))
        real_open = dlsym(RTLD_NEXT, "open");

    ut_push_task_ptr(&task_desc);
    ret = real_open(path, flags, mode);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

ssize_t
read(int fd, void * buf, size_t count)
{
    static ssize_t(*real_read)(int fd, void * buf, size_t count);
    static struct ut_task_desc task_desc = {
        .name = "read"
    };
    ssize_t ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real_read))
        real_read = dlsym(RTLD_NEXT, "read");

    ut_push_task_ptr(&task_desc);
    ret = real_read(fd, buf, count);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

ssize_t
write(int fd, const void * buf, size_t count)
{
    static ssize_t(*real_write)(int fd, const void * buf, size_t count);
    static struct ut_task_desc task_desc = {
        .name = "write"
    };
    ssize_t ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real_write))
        real_write = dlsym(RTLD_NEXT, "write");

    ut_push_task_ptr(&task_desc);
    ret = real_write(fd, buf, count);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

int
ioctl(int fd, unsigned long req, void * data)
{
    static int(*real_ioctl)(int fd, unsigned long req, void * data);
    static struct ut_task_desc task_desc = {
        .name = "ioctl"
    };
    int ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real_ioctl))
        real_ioctl = dlsym(RTLD_NEXT, "ioctl");

    ut_push_task_ptr(&task_desc);
    ret = real_ioctl(fd, req, data);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

int
nanosleep(const void * req, void * rem)
{
    static int(*real_nanosleep)(const void * req, void * rem);
    static struct ut_task_desc task_desc = {
        .name = "nanosleep"
    };
    int ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real_nanosleep))
        real_nanosleep = dlsym(RTLD_NEXT, "nanosleep");

    ut_push_task_ptr(&task_desc);
    ret = real_nanosleep(req, rem);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

ssize_t
send(int sockfd, const void * buf, size_t len, int flags)
{
    static ssize_t(*real_send)(int sockfd, const void * buf, size_t len, int flags);
    static struct ut_task_desc task_desc = {
        .name = "send"
    };
    ssize_t ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real_send))
        real_send = dlsym(RTLD_NEXT, "send");

    ut_push_task_ptr(&task_desc);
    ret = real_send(sockfd, buf, len, flags);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

ssize_t
sendto(int sockfd, const void * buf, size_t len, int flags, const void * dest_addr, int addrlen)
{
    static ssize_t(*real_sendto)(int sockfd, const void * buf, size_t len, int flags, const void * dest_addr, int addrlen);
    static struct ut_task_desc task_desc = {
        .name = "sendto"
    };
    ssize_t ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real_sendto))
        real_sendto = dlsym(RTLD_NEXT, "sendto");

    ut_push_task_ptr(&task_desc);
    ret = real_sendto(sockfd, buf, len, flags, dest_addr, addrlen);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

ssize_t
sendmsg(int sockfd, const void * msg, int flags)
{
    static ssize_t(*real_sendmsg)(int sockfd, const void * msg, int flags);
    static struct ut_task_desc task_desc = {
        .name = "sendmsg"
    };
    ssize_t ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real_sendmsg))
        real_sendmsg = dlsym(RTLD_NEXT, "sendmsg");

    ut_push_task_ptr(&task_desc);
    ret = real_sendmsg(sockfd, msg, flags);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

ssize_t
recvmsg(int socket, void * msg, int flags)
{
    static ssize_t(*real_recvmsg)(int socket, void * msg, int flags);
    static struct ut_task_desc task_desc = {
        .name = "recvmsg"
    };
    ssize_t ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real_recvmsg))
        real_recvmsg = dlsym(RTLD_NEXT, "recvmsg");

    ut_push_task_ptr(&task_desc);
    ret = real_recvmsg(socket, msg, flags);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

int
pthread_mutex_lock(void * mutex)
{
    static int(*real_pthread_mutex_lock)(void * mutex);
    static struct ut_task_desc task_desc = {
        .name = "pthread_mutex_lock"
    };
    int ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real_pthread_mutex_lock))
        real_pthread_mutex_lock = dlsym(RTLD_NEXT, "pthread_mutex_lock");

    ut_push_task_ptr(&task_desc);
    ret = real_pthread_mutex_lock(mutex);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

int
pthread_mutex_trylock(void * mutex)
{
    static int(*real_pthread_mutex_trylock)(void * mutex);
    static struct ut_task_desc task_desc = {
        .name = "pthread_mutex_trylock"
    };
    int ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real_pthread_mutex_trylock))
        real_pthread_mutex_trylock = dlsym(RTLD_NEXT, "pthread_mutex_trylock");

    ut_push_task_ptr(&task_desc);
    ret = real_pthread_mutex_trylock(mutex);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

int
pthread_mutex_unlock(void * mutex)
{
    static int(*real_pthread_mutex_unlock)(void * mutex);
    static struct ut_task_desc task_desc = {
        .name = "pthread_mutex_unlock"
    };
    int ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real_pthread_mutex_unlock))
        real_pthread_mutex_unlock = dlsym(RTLD_NEXT, "pthread_mutex_unlock");

    ut_push_task_ptr(&task_desc);
    ret = real_pthread_mutex_unlock(mutex);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

int
__ut_pthread_cond_wait_GLIBC_2_2_5(void * restrict cond, void * restrict mutex)
{
    static int(*real___ut_pthread_cond_wait_GLIBC_2_2_5)(void * restrict cond, void * restrict mutex);
    static struct ut_task_desc task_desc = {
        .name = "pthread_cond_wait"
    };
    int ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real___ut_pthread_cond_wait_GLIBC_2_2_5))
        real___ut_pthread_cond_wait_GLIBC_2_2_5 = dlvsym(RTLD_NEXT, "pthread_cond_wait", "GLIBC_2.2.5");

    ut_push_task_ptr(&task_desc);
    ret = real___ut_pthread_cond_wait_GLIBC_2_2_5(cond, mutex);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

int
__ut_pthread_cond_wait_GLIBC_2_3_2(void * restrict cond, void * restrict mutex)
{
    static int(*real___ut_pthread_cond_wait_GLIBC_2_3_2)(void * restrict cond, void * restrict mutex);
    static struct ut_task_desc task_desc = {
        .name = "pthread_cond_wait"
    };
    int ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real___ut_pthread_cond_wait_GLIBC_2_3_2))
        real___ut_pthread_cond_wait_GLIBC_2_3_2 = dlvsym(RTLD_NEXT, "pthread_cond_wait", "GLIBC_2.3.2");

    ut_push_task_ptr(&task_desc);
    ret = real___ut_pthread_cond_wait_GLIBC_2_3_2(cond, mutex);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

int
__ut_pthread_cond_timedwait_GLIBC_2_2_5(void * restrict cond, void * restrict mutex, void * restrict abstime)
{
    static int(*real___ut_pthread_cond_timedwait_GLIBC_2_2_5)(void * restrict cond, void * restrict mutex, void * restrict abstime);
    static struct ut_task_desc task_desc = {
        .name = "pthread_cond_timedwait"
    };
    int ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real___ut_pthread_cond_timedwait_GLIBC_2_2_5))
        real___ut_pthread_cond_timedwait_GLIBC_2_2_5 = dlvsym(RTLD_NEXT, "pthread_cond_timedwait", "GLIBC_2.2.5");

    ut_push_task_ptr(&task_desc);
    ret = real___ut_pthread_cond_timedwait_GLIBC_2_2_5(cond, mutex, abstime);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

int
__ut_pthread_cond_timedwait_GLIBC_2_3_2(void * restrict cond, void * restrict mutex, void * restrict abstime)
{
    static int(*real___ut_pthread_cond_timedwait_GLIBC_2_3_2)(void * restrict cond, void * restrict mutex, void * restrict abstime);
    static struct ut_task_desc task_desc = {
        .name = "pthread_cond_timedwait"
    };
    int ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real___ut_pthread_cond_timedwait_GLIBC_2_3_2))
        real___ut_pthread_cond_timedwait_GLIBC_2_3_2 = dlvsym(RTLD_NEXT, "pthread_cond_timedwait", "GLIBC_2.3.2");

    ut_push_task_ptr(&task_desc);
    ret = real___ut_pthread_cond_timedwait_GLIBC_2_3_2(cond, mutex, abstime);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

int
__ut_pthread_cond_signal_GLIBC_2_2_5(void * cond)
{
    static int(*real___ut_pthread_cond_signal_GLIBC_2_2_5)(void * cond);
    static struct ut_task_desc task_desc = {
        .name = "pthread_cond_signal"
    };
    int ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real___ut_pthread_cond_signal_GLIBC_2_2_5))
        real___ut_pthread_cond_signal_GLIBC_2_2_5 = dlvsym(RTLD_NEXT, "pthread_cond_signal", "GLIBC_2.2.5");

    ut_push_task_ptr(&task_desc);
    ret = real___ut_pthread_cond_signal_GLIBC_2_2_5(cond);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

int
__ut_pthread_cond_signal_GLIBC_2_3_2(void * cond)
{
    static int(*real___ut_pthread_cond_signal_GLIBC_2_3_2)(void * cond);
    static struct ut_task_desc task_desc = {
        .name = "pthread_cond_signal"
    };
    int ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real___ut_pthread_cond_signal_GLIBC_2_3_2))
        real___ut_pthread_cond_signal_GLIBC_2_3_2 = dlvsym(RTLD_NEXT, "pthread_cond_signal", "GLIBC_2.3.2");

    ut_push_task_ptr(&task_desc);
    ret = real___ut_pthread_cond_signal_GLIBC_2_3_2(cond);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

int
__ut_pthread_cond_broadcast_GLIBC_2_2_5(void * cond)
{
    static int(*real___ut_pthread_cond_broadcast_GLIBC_2_2_5)(void * cond);
    static struct ut_task_desc task_desc = {
        .name = "pthread_cond_broadcast"
    };
    int ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real___ut_pthread_cond_broadcast_GLIBC_2_2_5))
        real___ut_pthread_cond_broadcast_GLIBC_2_2_5 = dlvsym(RTLD_NEXT, "pthread_cond_broadcast", "GLIBC_2.2.5");

    ut_push_task_ptr(&task_desc);
    ret = real___ut_pthread_cond_broadcast_GLIBC_2_2_5(cond);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

int
__ut_pthread_cond_broadcast_GLIBC_2_3_2(void * cond)
{
    static int(*real___ut_pthread_cond_broadcast_GLIBC_2_3_2)(void * cond);
    static struct ut_task_desc task_desc = {
        .name = "pthread_cond_broadcast"
    };
    int ret;

    if (unlikely(!ut_push_task_ptr))
        load_libut();

    if (unlikely(!real___ut_pthread_cond_broadcast_GLIBC_2_3_2))
        real___ut_pthread_cond_broadcast_GLIBC_2_3_2 = dlvsym(RTLD_NEXT, "pthread_cond_broadcast", "GLIBC_2.3.2");

    ut_push_task_ptr(&task_desc);
    ret = real___ut_pthread_cond_broadcast_GLIBC_2_3_2(cond);
    ut_pop_task_ptr(&task_desc);

    return ret;
}

__asm__(".symver __ut_pthread_cond_wait_GLIBC_2_2_5, pthread_cond_wait@GLIBC_2.2.5");
__asm__(".symver __ut_pthread_cond_wait_GLIBC_2_3_2, pthread_cond_wait@@GLIBC_2.3.2");
__asm__(".symver __ut_pthread_cond_timedwait_GLIBC_2_2_5, pthread_cond_timedwait@GLIBC_2.2.5");
__asm__(".symver __ut_pthread_cond_timedwait_GLIBC_2_3_2, pthread_cond_timedwait@@GLIBC_2.3.2");
__asm__(".symver __ut_pthread_cond_signal_GLIBC_2_2_5, pthread_cond_signal@GLIBC_2.2.5");
__asm__(".symver __ut_pthread_cond_signal_GLIBC_2_3_2, pthread_cond_signal@@GLIBC_2.3.2");
__asm__(".symver __ut_pthread_cond_broadcast_GLIBC_2_2_5, pthread_cond_broadcast@GLIBC_2.2.5");
__asm__(".symver __ut_pthread_cond_broadcast_GLIBC_2_3_2, pthread_cond_broadcast@@GLIBC_2.3.2");
