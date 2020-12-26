#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "libshmlog.h"

static int g_requestExit = 0;

void sig_handle(int sig)
{
    fprintf(stderr, "receive signal %d\n", sig);
    g_requestExit = 1;
}

int main(int argc, char *argv[])
{
    static const char *usage = "Usage: dtracetail [options]... [pid]\n"\
            "output the last party of dtrace ring buffer.\n\n"         \
            "options:\n\t-h,--help\n\t-p,--pid <number>\n";
    static struct option opts[] = {
        {"help", 0, NULL, 'h'},
        {"pid", 1, NULL, 'p'},
        {NULL, 0, NULL, 0}
    };
    pid_t pid = -1;
    int o, fd;
    char filename[256];
    struct stat statbuf;
    volatile struct shmlog_header *hdr;
    volatile struct shmlog_msg *msgs;
    uint16_t size, last, next, cnt, idx;
    int64_t total_lost, total_lost_cnt;

    // test
    for ( o = 0; o < argc; o++ ) {
        fprintf(stderr, "%s ", argv[o]);
    }
    fprintf(stderr, "\n");
    
    // command line parse
    opterr = 0;
    while ( (o = getopt_long(argc, argv, ":hp:", opts, NULL)) != EOF ) {
        switch ( o ) {
            case 'h':
                puts(usage);
                return 0;
            case 'p':
                if ( sscanf(optarg, "%d", &pid) != 1 || pid <= 0 ) {
                    fprintf(stderr, "Error: invalid pid '%s'!\n", optarg);
                    return 1;
                }
                break;
            case ':':
                fprintf(stderr, "Error: missing option argument for option '%s'!\n", argv[optind-1]);
                return 1;
            case '?':
                fprintf(stderr, "Error: unknown options '%s'!\n", argv[optind-1]);
                return 1;
            default:
                fprintf(stderr, "Warning: unknown options '%c'!\n", o);
        }
    }
    if ( optind < argc ) {
        pid_t pid2;
        if ( sscanf(argv[optind], "%d", &pid2) != 1 || pid2 <= 0 ) {
            fprintf(stderr, "Error: invalid pid '%s'!\n", argv[optind]);
            return 1;
        }
        if ( pid > 0 ) {
            fprintf(stderr, "Warning: Previous pid '%d' are overwritten!\n", pid);
        }
        pid = pid2;
        if ( (optind+1) < argc ) {
            fprintf(stderr, "Error: unknown argument '%s'!\n", argv[optind+1]);
            return 1;
        }
    }
    if ( pid <= 0 ) {
        fprintf(stderr, "pid is not specify!\n");
        return 1;
    }
    fprintf(stderr, "pid = %d\n", pid);

    // open shm
    snprintf(filename, sizeof(filename), "/dev/shm/" SHM_FILE_PREFIX "%d", pid);
    if ( stat(filename, &statbuf) < 0 ) {
        fprintf(stderr, "Error: stat! %d: %s\n", errno, strerror(errno));
        return 1;
    }
    fprintf(stderr, "%s: %ld bytes\n", filename, statbuf.st_size);
    fprintf(stderr, "%s: mode=%08x\n", filename, statbuf.st_mode);
    if ( statbuf.st_size < sizeof(struct shmlog_fullheader) ) {
        fprintf(stderr, "Error: invalid share memory size %ld!\n", statbuf.st_size);
        return 1;
    }
    snprintf(filename, sizeof(filename), SHM_FILE_PREFIX "%d", pid);
    fd = shm_open(filename, O_RDONLY, 0666);
    if ( fd < 0 ) {
        fprintf(stderr, "Error: call shm_open failed! %d:%s\n", errno, strerror(errno));
        return 1;
    }
    hdr = (struct shmlog_header *)mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if ( NULL == hdr ) {
        fprintf(stderr, "Error: call mmap failed! %d:%s\n", errno, strerror(errno));
        return 1;
    }

    // check ring buffer
    if ( (sizeof(struct shmlog_fullheader) + hdr->size * sizeof(struct shmlog_msg)) > statbuf.st_size ) {
        fprintf(stderr, "Error: invalid ring buffer size %d!\n", hdr->size);
        munmap((void*)hdr, statbuf.st_size);
        return 1;
    }
    msgs = (struct shmlog_msg *)((uint8_t*)hdr + sizeof(struct shmlog_fullheader));

    // install signal handle
    signal(SIGINT, sig_handle);

    // core loop
    size = hdr->size;
    last = hdr->next;
    cnt = 0;
    total_lost = 0;
    total_lost_cnt = 0;
    while ( !g_requestExit ) {

        if ( 0 == cnt ) {
            usleep(1000*10);
        }

        next = hdr->next;
        cnt = next - last;
        if ( cnt > size ) {
            total_lost += cnt - size;
            total_lost_cnt++;
            //fprintf(stderr, "Error: lost bucket message! %d, %ld\n", cnt - size, total_lost);
            last = next - size;
        }
        if ( next != last ) {
            for ( ; last != next; last++ ) {
                idx = last % size;
                fwrite((void*)msgs[idx].body, 1, msgs[idx].hdr.len, stdout);
                fwrite("\n", 1, 1, stdout);
            }
        }
    }

    // finish
    munmap((void*)hdr, statbuf.st_size);
    close(fd);
    fprintf(stderr, "total lost %ld, %ld times\n", total_lost, total_lost_cnt);
    return 0;
}
