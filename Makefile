CFLAGS=-g3 -O0 -DDEBUG -I.

all: libut.so server libfakeGL.so apiwrappers.so

libut.so: ut.c gputop-list.c ut-utils.c memfd.c ut-memfd-array.c
	$(CC) -shared -Wl,-soname="libut.so.1" -fPIC -o $@ $^ $(CFLAGS) -pthread -ldl

apiwrappers.so: ut-api-wrappers.c libut.so
	$(CC) -shared -fPIC -o $@ $(filter %.c,$^) $(CFLAGS) -L. -lut

libfakeGL.so: gputop-gl.c registry/glxapi.c registry/glapi.c libut.so
	$(CC) -shared -Wl,-soname="libGL.so.1" -fPIC -o $@ $(filter %.c,$^) $(CFLAGS) -L. -lut

server: ut-server.c ut-utils.c memfd.c jsmn.c
	$(CC) -o $@ $^ $(CFLAGS) `pkg-config --cflags --libs libuv`

clean:
	-rm -f *.o *.so server
