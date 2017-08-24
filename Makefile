CFLAGS=-g3 -O0 -DDEBUG

all: ut.so server

ut.so: ut.c gputop-list.c ut-utils.c memfd.c ut-api-wrappers.c ut-memfd-array.c
	$(CC) -shared -fPIC -o $@ $^ $(CFLAGS)

server: ut-server.c ut-utils.c memfd.c
	$(CC) -o $@ $^ $(CFLAGS) `pkg-config --cflags --libs libuv`
