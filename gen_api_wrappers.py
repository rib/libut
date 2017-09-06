#!/usr/bin/env python

#void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
#int open(const char *pathname, int flags, mode_t mode);
#ssize_t read(int fd, void *buf, size_t count);
#ssize_t write(int fd, const void *buf, size_t count);
#int ioctl(int fd, unsigned long request, void *data);
#int nanosleep(const struct timespec *rqtp, struct timespec *rmtp);
#void *malloc(size_t size)
#void free(void *ptr)
#void *calloc(size_t nmemb, size_t size)
#void *realloc(void *ptr, size_t size)
#ssize_t send(int sockfd, const void *buf, size_t len, int flags);
#ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
#       const struct sockaddr *dest_addr, socklen_t addrlen);
#ssize_t sendmsg(int sockfd, const struct msghdr *msg, int flags);
#ssize_t recvmsg(int socket, struct msghdr *message, int flags);
#int pthread_mutex_lock(pthread_mutex_t *mutex);
#int pthread_mutex_trylock(pthread_mutex_t *mutex);
#int pthread_mutex_unlock(pthread_mutex_t *mutex);
#int pthread_cond_wait(pthread_cond_t * restrict cond
#                        pthread_mutex_t * restrict mutex);
#XXX: note the pthread_cond_ apis have LinuxThreads vs NPTL versions
#int pthread_cond_timedwait(pthread_cond_t * restrict cond
#                        pthread_mutex_t * restrict mutex,
#                        const struct timespec * restrict abstime);
#int pthread_cond_signal(pthread_cond_t *cond);
#int pthread_cond_broadcastpthread_cond_t *cond);

apis = [
    {
        "name": "mmap",
        "args": [
            [ 'void *', 'addr' ],
            [ 'size_t', 'len' ],
            [ 'int', 'prot' ],
            [ 'int', 'flags' ],
            [ 'int', 'fd' ],
            [ 'off_t', 'offset' ],
        ],
        "ret": 'void *'
    },
    {
        "name": "open",
        "args": [
            [ 'const char *', 'path' ],
            [ 'int', 'flags' ],
            [ 'mode_t', 'mode' ],
        ],
        "ret": 'int'
    },
    {
        "name": "read",
        "args": [
            [ 'int', 'fd' ],
            [ 'void *', 'buf' ],
            [ 'size_t', 'count' ],
        ],
        "ret": 'ssize_t'
    },
    {
        "name": "write",
        "args": [
            [ 'int', 'fd' ],
            [ 'const void *', 'buf' ],
            [ 'size_t', 'count' ],
        ],
        "ret": 'ssize_t'
    },
    {
        "name": "ioctl",
        "args": [
            [ 'int', 'fd' ],
            [ 'unsigned long', 'req' ],
            [ 'void *', 'data' ],
        ],
        "ret": 'int'
    },
    {
        "name": "nanosleep",
        "args": [
            [ 'const void *', 'req' ],
            [ 'void *', 'rem' ],
        ],
        "ret": 'int'
    },
    {
        "name": "send",
        "args": [
            [ 'int', 'sockfd' ],
            [ 'const void *', 'buf' ],
            [ 'size_t', 'len' ],
            [ 'int', 'flags' ],
        ],
        "ret": 'ssize_t'
    },
    {
        "name": "sendto",
        "args": [
            [ 'int', 'sockfd' ],
            [ 'const void *', 'buf' ],
            [ 'size_t', 'len' ],
            [ 'int', 'flags' ],
            [ 'const void *', 'dest_addr' ],
            [ 'int', 'addrlen' ],
        ],
        "ret": 'ssize_t'
    },
    {
        "name": "sendmsg",
        "args": [
            [ 'int', 'sockfd' ],
            [ 'const void *', 'msg' ],
            [ 'int', 'flags' ],
        ],
        "ret": 'ssize_t'
    },
    {
        "name": "recvmsg",
        "args": [
            [ 'int', 'socket' ],
            [ 'void *', 'msg' ],
            [ 'int', 'flags' ],
        ],
        "ret": 'ssize_t'
    },
    {
        "name": "pthread_mutex_lock",
        "args": [
            [ 'void *', 'mutex' ],
        ],
        "ret": 'int'
    },
    {
        "name": "pthread_mutex_trylock",
        "args": [
            [ 'void *', 'mutex' ],
        ],
        "ret": 'int'
    },
    {
        "name": "pthread_mutex_unlock",
        "args": [
            [ 'void *', 'mutex' ],
        ],
        "ret": 'int'
    },
    {
        "name": "pthread_cond_wait",
        "args": [
            [ 'void * restrict', 'cond' ],
            [ 'void * restrict', 'mutex' ],
        ],
        "ret": 'int',
        "versions": [ "GLIBC_2.2.5", "GLIBC_2.3.2" ]
    },
    {
        "name": "pthread_cond_timedwait",
        "args": [
            [ 'void * restrict', 'cond' ],
            [ 'void * restrict', 'mutex' ],
            [ 'void * restrict', 'abstime' ],
        ],
        "ret": 'int',
        "versions": [ "GLIBC_2.2.5", "GLIBC_2.3.2" ]
    },
    {
        "name": "pthread_cond_signal",
        "args": [
            [ 'void *', 'cond' ],
        ],
        "ret": 'int',
        "versions": [ "GLIBC_2.2.5", "GLIBC_2.3.2" ]
    },
    {
        "name": "pthread_cond_broadcast",
        "args": [
            [ 'void *', 'cond' ],
        ],
        "ret": 'int',
        "versions": [ "GLIBC_2.2.5", "GLIBC_2.3.2" ]
    },

]

whitelist = [
    "mmap",
    "open",
    "read",
    "write",
    "ioctl",
    "nanosleep",
    "send",
    "sendto",
    "sendmsg",
    "recvmsg",
    "pthread_mutex_lock",
    "pthread_mutex_trylock",
    "pthread_mutex_unlock",
    "pthread_cond_wait",
    "pthread_cond_timedwait",
    "pthread_cond_signal",
    "pthread_cond_broadcast",
]


def emit_wrapper(func, symname, ver=None):
    if 'ret' in func:
        rettype = func['ret']
    else:
        rettype = "void"

    print(rettype)

    args=""
    for arg in func['args']:
        args = args + arg[0] + " " + arg[1] + ", "
    if args == "":
        args = "void"
    else:
        args = args[:-2]

    print(symname + "(" + args + ")")
    print("{")
    print("    static " + rettype + "(*real_" + symname + ")(" + args + ");")
    print("    static struct ut_task_desc task_desc = {")
    print("        .name = \"" + func['name'] + "\"")
    print("    };")

    if 'ret' in func:
        print("    " + rettype + " ret;")

    print("")
    print("    if (unlikely(!ut_push_task_ptr))")
    print("        load_libut();")
    print("")
    print("    if (unlikely(!real_" + symname + "))")
    if ver == None:
        print("        real_" + symname + " = dlsym(RTLD_NEXT, \"" + func['name'] + "\");")
    else:
        print("        real_" + symname + " = dlvsym(RTLD_NEXT, \"" + func['name'] + "\", \"" + ver + "\");")
    print("")
    print("    ut_push_task_ptr(&task_desc);")
    names=""
    for arg in func['args']:
        names = names + arg[1] + ", "
    if names != "":
        names = names[:-2]

    if 'ret' in func:
        print("    ret = real_" + symname + "(" + names + ");")
    else:
        print("    real_" + symname + "(" + names + ");")
    print("    ut_pop_task_ptr(&task_desc);")

    print("")
    if 'ret' in func:
        print("    return ret;")
    print("}")
    print("")


for func in apis:
    if func['name'] not in whitelist:
        continue

    if "versions" in func:
        for ver in func['versions']:
            vername = "__ut_" + func['name'] + "_" + ver.replace('.', '_');
            emit_wrapper(func, vername, ver)
    else:
        emit_wrapper(func, func['name'])


for func in apis:
    if func['name'] not in whitelist:
        continue

    if "versions" not in func:
        continue

    first = True
    for ver in func['versions']:
        vername = "__ut_" + func['name'] + "_" + ver.replace('.', '_');
        if first:
            sep = "@"
        else:
            sep = "@@"
        print('asm(".symver ' + vername + ', ' + func['name'] + sep + ver + '");')
        first = False
