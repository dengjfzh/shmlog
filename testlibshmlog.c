#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <threads.h>
#include <unistd.h>
#include <sys/time.h>
#include "libshmlog.h"

int main(int argc, char *argv[])
{
    char msg[1024];
    int cnt, failed_cnt, i, len, ret;
    struct timeval start, end, escape;
    double fEscape;
    fprintf(stderr, "testlibshmlog: start ...\n");
    cnt = 200;
    failed_cnt = 0;
    if ( argc > 1 ) {
        cnt = atoi(argv[1]);
        if ( cnt <= 0 ) {
            fprintf(stderr, "testlibshmlog: Error: invalid number: %s\n", argv[1]);
        }
    }
    shmlog_init(16384);
    usleep(1000*500);
    gettimeofday(&start, NULL);
    for ( i = 0; i < cnt; i++ ) {
        //len = sprintf(msg, "msg %d", i);
        memset(msg, '0' + i%10, 400);
        len = 400;
        ret = shmlog_write(msg, len);
        if ( ret <= 0 ) {
            failed_cnt++;
        }
        //usleep(1);
        //thrd_yield();
    }
    gettimeofday(&end, NULL);
    escape.tv_sec = end.tv_sec - start.tv_sec;
    if ( end.tv_usec > start.tv_usec ) {
        escape.tv_usec = end.tv_usec - start.tv_usec;
    } else {
        escape.tv_usec = 1000000 + end.tv_usec - start.tv_usec;
        escape.tv_sec--;
    }
    fEscape = escape.tv_sec + escape.tv_usec / 1000000.0;
    len = snprintf(msg, sizeof(msg), "testlibshmlog: end. %d log, %d failed, escape %ld.%06lds, %.1f/s", i, failed_cnt, escape.tv_sec, escape.tv_usec, i/fEscape);
    shmlog_write(msg, len);
    shmlog_uninit();
    fprintf(stderr, "%s\n", msg);
    return 0;
}
