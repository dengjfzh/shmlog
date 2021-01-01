CFLAGS ?= -ggdb3 -O0
override CFLAGS += -fPIC -Wall -std=gnu11
override LDFLAGS += -lrt

.PHONY: all
all: libshmlog.so testlibshmlog shmlogtail

libshmlog.so: libshmlog.o
	$(CC) $(LDFLAGS) -shared -o $@ $^

testlibshmlog: testlibshmlog.o libshmlog.so
	$(CC) -L. -Wl,-rpath,'$$ORIGIN' -lshmlog -o $@ testlibshmlog.o

shmlogtail: shmlogtail.o

.PHONY: test
test: testlibshmlog shmlogtail
	./testlibshmlog 1000000 &
	./shmlogtail `pidof testlibshmlog`

.PHONY: clean
clean:
	@rm -f *.o libshmlog.so shmlogtail testlibshmlog
