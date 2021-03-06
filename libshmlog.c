#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <threads.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include "libshmlog.h"

#define SHM_FILE_PATH "/dev/shm"
#define FULL_RETRY_MAX 16

static int g_regAtexit = 0;
static int g_fd = -1;
static void *g_addr = MAP_FAILED;
static size_t g_size = 0;
static struct shmlog_header *g_hdr = NULL;
static struct shmlog_msg *g_msgs = NULL;
static size_t g_remove_unused = 0;

#if 1
#define LOG(fmt, arg...) fprintf(stderr, fmt, ##arg)
#else
#define LOG(fmt, arg...)
#endif

#define GET_HEAD(ht) SHMLOG_GET_HEAD(ht)
#define GET_TAIL(ht) SHMLOG_GET_TAIL(ht)
#define MAKE_HT(head, tail) SHMLOG_MAKE_HT(head, tail)
#define INTHEAD_MAX SHMLOG_INTHEAD_MAX


static void onexit()
{
    if ( g_fd > 0 ) {
        char filename[256];
        snprintf(filename, sizeof(filename), SHMLOG_FILE_PREFIX "%d", getpid());
        shm_unlink(filename);
        close(g_fd);
        g_fd = -1;
    }
}

static int unlink_all_unuse()
{
    char filename[256];
    struct stat statbuf;
    DIR *dir;
    struct dirent *dent;
    int errbak, pid;
    dir = opendir(SHM_FILE_PATH);
    if ( NULL == dir ) {
        return -1;
    }
    errno = 0;
    while ( (dent = readdir(dir)) != NULL ) {
        //LOG("%s: ino=%ld, off=%ld, reclen=%d, type=0x%02x\n",
        //    dent->d_name, dent->d_ino, dent->d_off, dent->d_reclen, dent->d_type);
        if ( DT_REG == dent->d_type ) {
            //LOG("find shm: %s, type=0x%x\n", dent->d_name, dent->d_type);
            if ( sscanf(dent->d_name, SHMLOG_FILE_PREFIX "%d", &pid) == 1 && pid > 0 ) {
                snprintf(filename, sizeof(filename), "/proc/%d", pid);
                //LOG("found shm with pid %d\n", pid);
                if ( stat(filename, &statbuf) == -1 && ENOENT == errno ) {
                    LOG("found shm with pid %d, but process is not exist!\n", pid);
                    snprintf(filename, sizeof(filename), SHMLOG_FILE_PREFIX "%d", pid);
                    if ( shm_unlink(filename) >= 0 ) {
                        LOG("file '%s' has been deleted.\n", filename);
                    } else {
                        LOG("delete file '%s' failed.\n", filename);
                    }
                }
            }
        }
    }
    errbak = errno;
    closedir(dir);
    errno = errbak;
    if ( 0 != errno ) {
        return -1;
    }
    return 0;
}

int shmlog_init(size_t nmsg, int remove_unused)
{
    int errno_bak;
    char filename[256];
    const size_t size = sizeof(struct shmlog_fullheader) + SHMLOG_MSG_SIZE * nmsg;
    if ( remove_unused ) {
        unlink_all_unuse();
    }
    if ( g_fd > 0 ) {
        return -1;
    }
    // clear global variables
    g_hdr = NULL;
    g_msgs = NULL;
    g_remove_unused = remove_unused;
    // open shm
    snprintf(filename, sizeof(filename), SHMLOG_FILE_PREFIX "%d", getpid());
    g_fd = shm_open(filename, O_CREAT|O_RDWR, 0666);
    if ( g_fd < 0 ) {
        goto FAILED;
    }
    if ( ftruncate(g_fd, size) < 0 ) {
        goto FAILED;
    }
    g_addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd, 0);
    if ( MAP_FAILED == g_addr ) {
        goto FAILED;
    }
    // setup global variables
    g_size = size;
    g_hdr = (struct shmlog_header *)g_addr;
    g_hdr->nmsg = nmsg;
    atomic_init(&g_hdr->consumer_pid, 0);
    atomic_init(&g_hdr->headtail, 0);
    g_msgs = (struct shmlog_msg *)(g_addr + sizeof(struct shmlog_fullheader));
    for ( size_t i = 0; i < nmsg; i++ ) {
        atomic_init(&g_msgs[i].hdr.filled, false);
    }
    // register an exit function to unlink shm
    if ( 0 == g_regAtexit ) {
        g_regAtexit = 1;
        atexit(onexit);
    }
    return 0;
FAILED:
    errno_bak = errno;
    if ( MAP_FAILED != g_addr ) {
        munmap(g_addr, size);
        g_addr = MAP_FAILED;
    }
    g_size = 0;
    if ( g_fd > 0 ) {
        close(g_fd);
        g_fd = -1;
        snprintf(filename, sizeof(filename), SHMLOG_FILE_PREFIX "%d", getpid());
        shm_unlink(filename);
    }
    errno = errno_bak;
    return -1;
}

void shmlog_uninit()
{
    char filename[256];
    if ( g_fd < 0 ) {
        return;
    }
    int fd = g_fd;
    g_fd = -1;
    g_hdr = NULL;
    g_msgs = NULL;
    if ( MAP_FAILED != g_addr ) {
        munmap(g_addr, g_size);
        g_addr = MAP_FAILED;
    }
    g_size = 0;
    if ( fd > 0 ) {
        close(fd);
        snprintf(filename, sizeof(filename), SHMLOG_FILE_PREFIX "%d", getpid());
        shm_unlink(filename);
    }
    if ( g_remove_unused ) {
        unlink_all_unuse();
    }
}

int shmlog_write(const void *data, size_t len)
{
    shmlog_int_headtail ht_old, ht_new;
    shmlog_int_head head, tail, head_new, tail_new;
    bool full;
    int full_retry, full_wait;
    if ( g_fd < 0 || NULL == g_hdr || NULL == g_msgs ) {
        return -1;
    }
    if ( len > SHMLOG_MSG_BODY_SIZE ) {
        len = SHMLOG_MSG_BODY_SIZE;
    }
    full_retry = 0;
    full_wait = 1;
    ht_old = atomic_load(&g_hdr->headtail);
    do {
        head = GET_HEAD(ht_old);
        tail = GET_TAIL(ht_old);
        if ( head > tail ) {
            LOG("[dengjfzh/libshmlog] Internal Error: head(%u) > tail(%u)! %s:%d\n",
                head, tail, __FILE__, __LINE__);
            abort();
        }
        head_new = head;
        tail_new = tail + 1;
        if ( (tail_new - head_new) > g_hdr->nmsg ) { // full
            const pid_t consumer_pid = atomic_load(&g_hdr->consumer_pid);
            if ( consumer_pid > 0 ) {
                if ( full_retry < FULL_RETRY_MAX ) {
                    // wait a moment if there is a consumer
                    full = true;
                    full_retry++;
                    if ( full_wait < 65536 ) {
                        full_wait *= 2;
                    }
                    usleep(full_wait);
                    ht_old = atomic_load(&g_hdr->headtail);
                    continue;
                }
                // consumer timeout, remve it
                if ( kill(consumer_pid, 0) < 0 && ESRCH == errno ) {
                    if ( atomic_compare_exchange_strong(&g_hdr->consumer_pid, &consumer_pid, 0) ) {
                        LOG("[dengjfzh/libshmlog] Warning: consumer %d has been removed! %s:%d\n",
                            consumer_pid, __FILE__, __LINE__);
                    }
                }
            }
            // overwrite oldest msg
            head_new = head + 1;
            if ( head_new >= INTHEAD_MAX ) {
                shmlog_int_head tmp = (head_new / g_hdr->nmsg) * g_hdr->nmsg;
                head_new -= tmp;
                tail_new -= tmp;
            }
        }
        full = false;
        full_retry = 0;
        full_wait = 1;
        ht_new = MAKE_HT(head_new, tail_new);
    } while ( full || !atomic_compare_exchange_weak(&g_hdr->headtail, &ht_old, ht_new) );
    if ( head_new != head ) { // oldest msg has been removed
        head %= g_hdr->nmsg;
        atomic_store(&g_msgs[head].hdr.filled, false);
    }
    if ( tail >= g_hdr->nmsg ) {
        tail %= g_hdr->nmsg;
    }
    while ( atomic_load(&g_msgs[tail].hdr.filled) ) {
        thrd_yield();
    }
    memcpy(g_msgs[tail].body, data, len);
    g_msgs[tail].hdr.len = len;
    atomic_store(&g_msgs[tail].hdr.filled, true);
    return len;
}

int shmlog_vprintf(const char *fmt, va_list ap)
{
    char msg[SHMLOG_MSG_BODY_SIZE];
    int len;
    len = vsnprintf(msg, sizeof(msg), fmt, ap);
    if ( len < 0 )
        return len;
    return shmlog_write(msg, len);
}

int shmlog_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = shmlog_vprintf(fmt, ap);
    va_end(ap);
    return ret;
}
