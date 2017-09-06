CFLAGS=-g3 -O0 -DDEBUG -I. -DENABLE_VALGRIND_MEMFD_WORKAROUND

all: libut.so libut-sysapiwrappers.so libut-wrapperGL.so ut-server

libut.so: ut.c gputop-list.c ut-utils.c memfd.c ut-memfd-array.c
	$(CC) -shared -Wl,-soname="libut.so.1" -fPIC -o $@ $^ $(CFLAGS) -pthread -ldl

libut-sysapiwrappers.so: ut-api-wrappers.c libut.so version.txt
	$(CC) -shared -fPIC -o $@ $(filter %.c,$^) $(CFLAGS) -Wl,--version-script -Wl,version.txt

libut-wrapperGL.so: gputop-gl.c registry/glxapi.c registry/glapi.c libut.so
	$(CC) -shared -Wl,-soname="libGL.so.1" -fPIC -o $@ $(filter %.c,$^) $(CFLAGS) -L. -lut

ut-server: ut-server.c ut-utils.c memfd.c json.c gputop-list.c
	$(CC) -o $@ $^ $(CFLAGS) `pkg-config --cflags --libs libuv`

clean:
	-rm -f *.o *.so ut-server
