#!/usr/bin/env python

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
        "name": "recv",
        "args": [
            [ 'int', 'socket' ],
            [ 'void *', 'buf' ],
            [ 'size_t', 'len' ],
            [ 'int', 'flags' ],
        ],
        "ret": 'ssize_t'
    },
    {
        "name": "recvfrom",
        "args": [
            [ 'int', 'socket' ],
            [ 'void * restrict', 'buf' ],
            [ 'size_t', 'len' ],
            [ 'int', 'flags' ],
            [ 'void * restrict', 'addr' ],
            [ 'int * restrict', 'addr_len' ],
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

    #XXX: note the pthread_cond_ apis have LinuxThreads vs NPTL versions
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

    {
        "name": "poll",
        "args": [
            [ 'void *', 'fds' ],
            [ 'unsigned long', 'nfds' ],
            [ 'int', 'timeout' ],
        ],
        "ret": 'int',
    },
    {
        "name": "ppoll",
        "args": [
            [ 'void *', 'fds' ],
            [ 'unsigned long', 'nfds' ],
            [ 'void *', 'tmo_p' ],
            [ 'void *', 'sigmask' ],
        ],
        "ret": 'int',
    },
    {
        "name": "epoll_wait",
        "args": [
            [ 'int', 'epfd' ],
            [ 'void *', 'events' ],
            [ 'int', 'maxevents' ],
            [ 'int', 'timeout' ],
        ],
        "ret": 'int',
    },
    {
        "name": "epoll_pwait",
        "args": [
            [ 'int', 'epfd' ],
            [ 'void *', 'events' ],
            [ 'int', 'maxevents' ],
            [ 'int', 'timeout' ],
            [ 'void *', 'sigmask' ],
        ],
        "ret": 'int',
    },
    {
        "name": "select",
        "args": [
            [ 'int', 'nfds' ],
            [ 'fd_set * restrict', 'readfds' ],
            [ 'fd_set * restrict', 'writefds' ],
            [ 'fd_set * restrict', 'errorfds' ],
            [ 'struct timeval * restrict', 'timeout' ],
        ],
        "ret": 'int',
    },
    {
        "name": "pselect",
        "args": [
            [ 'int', 'nfds' ],
            [ 'fd_set * restrict', 'readfds' ],
            [ 'fd_set * restrict', 'writefds' ],
            [ 'fd_set * restrict', 'errorfds' ],
            [ 'const struct timespec * restrict', 'timeout' ],
            [ 'const sigset_t *', 'sigmask' ],
        ],
        "ret": 'int',
    },
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


print("#define _GNU_SOURCE")
print("#include <sys/types.h>")
print("#include <dlfcn.h>")
print("")
print("#include \"ut-shared-data.h\"")
print("")
print("/* AUTOMATICALLY GENERATED; DO NOT EDIT */")
print("")
print("")
print("#define unlikely(x) __builtin_expect(x, 0)")
print("")
print("void load_libut(void);")
print("")
print("extern void (*ut_push_task_ptr)(struct ut_task_desc *task_desc);")
print("extern void (*ut_pop_task_ptr)(struct ut_task_desc *task_desc);")
print("")
print("")
print("")


for func in apis:
    if 'skip' in func and func['skip'] == True:
        continue

    if "versions" in func:
        for ver in func['versions']:
            vername = "__ut_" + func['name'] + "_" + ver.replace('.', '_');
            emit_wrapper(func, vername, ver)
    else:
        emit_wrapper(func, func['name'])


for func in apis:
    if 'skip' in func and func['skip'] == True:
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
        print('__asm__(".symver ' + vername + ', ' + func['name'] + sep + ver + '");')
        first = False
