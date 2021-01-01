CFLAGS ?= -ggdb3 -O0
override CFLAGS += -fPIC -Wall -std=gnu11

.PHONY: all
all: libshmlog.so libshmlogclient.so shmlogtail testlibshmlog

libshmlog.so: libshmlog.o
	$(CC) $(LDFLAGS) -lrt -shared -o $@ $^

libshmlogclient.so: libshmlogclient.o
	$(CC) $(LDFLAGS) -lrt -shared -o $@ $^

testlibshmlog: testlibshmlog.o libshmlog.so
	$(CC) $(LDFLAGS) -L. -Wl,-rpath,'$$ORIGIN' -lshmlog -o $@ testlibshmlog.o

shmlogtail: shmlogtail.o libshmlogclient.so
	$(CC) $(LDFLAGS) -lrt -L. -Wl,-rpath,'$$ORIGIN' -lshmlogclient -o $@ shmlogtail.o


libshmlog.o: libshmlog.c libshmlog.h
libshmlogclient.o: libshmlogclient.c libshmlogclient.h
shmlogtail.o: shmlogtail.c libshmlog.h
testlibshmlog.o: testlibshmlog.c libshmlog.h


.PHONY: test
test: testlibshmlog shmlogtail
	./testlibshmlog $(TESTCNT) &
	./shmlogtail `pidof testlibshmlog`

.PHONY: clean
clean:
	@rm -f *.o libshmlog.so libshmlogclient.so testlibshmlog shmlogtail

TESTCNT := 1000000
