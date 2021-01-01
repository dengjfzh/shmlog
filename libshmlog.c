#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
// #include <stdatomic.h> // C11
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include "libshmlog.h"

#define SHM_FILE_PATH "/dev/shm"

static int g_regAtexit = 0;
static int g_fd = -1;
static void *g_addr = MAP_FAILED;
static size_t g_size = 0;
static struct shmlog_header *g_hdr = NULL;
static struct shmlog_msg *g_msgs = NULL;
static size_t g_msg_cnt = 0;

#if 1
#define LOG(fmt, arg...) fprintf(stderr, fmt, ##arg)
#else
#define LOG(fmt, arg...)
#endif

static void onexit()
{
    if ( g_fd > 0 ) {
        char filename[256];
        snprintf(filename, sizeof(filename), SHM_FILE_PREFIX "%d", getpid());
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
        LOG("%s: ino=%ld, off=%ld, reclen=%d, type=0x%02x\n",
            dent->d_name, dent->d_ino, dent->d_off, dent->d_reclen, dent->d_type);
        if ( DT_REG == dent->d_type ) {
            LOG("find shm: %s, type=0x%x\n", dent->d_name, dent->d_type);
            if ( sscanf(dent->d_name, SHM_FILE_PREFIX "%d", &pid) == 1 && pid > 0 ) {
                snprintf(filename, sizeof(filename), "/proc/%d", pid);
                LOG("found shm with pid %d\n", pid);
                if ( stat(filename, &statbuf) == -1 && ENOENT == errno ) {
                    LOG("process %d not found!\n", pid);
                    snprintf(filename, sizeof(filename), SHM_FILE_PREFIX "%d", pid);
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

int shmlog_init(size_t nmsg)
{
    int errno_bak;
    char filename[256];
    const size_t size = sizeof(struct shmlog_fullheader) + SHMLOG_MSG_SIZE * nmsg;
    unlink_all_unuse();
    if ( g_fd > 0 ) {
        return -1;
    }
    // clear global variables
    g_hdr = NULL;
    g_msgs = NULL;
    g_msg_cnt = 0;
    // open shm
    snprintf(filename, sizeof(filename), SHM_FILE_PREFIX "%d", getpid());
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
    atomic_init(&g_hdr->next, 0);
    g_msgs = (struct shmlog_msg *)(g_addr + sizeof(struct shmlog_fullheader));
    g_msg_cnt = nmsg;
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
        snprintf(filename, sizeof(filename), SHM_FILE_PREFIX "%d", getpid());
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
    g_msg_cnt = 0;
    if ( MAP_FAILED != g_addr ) {
        munmap(g_addr, g_size);
        g_addr = MAP_FAILED;
    }
    g_size = 0;
    if ( fd > 0 ) {
        close(fd);
        snprintf(filename, sizeof(filename), SHM_FILE_PREFIX "%d", getpid());
        shm_unlink(filename);
    }
    unlink_all_unuse();
}

int shmlog_write(const void *data, size_t len)
{
    if ( g_fd < 0 || NULL == g_hdr || NULL == g_msgs || 0 == g_msg_cnt ) {
        return -1;
    }
    if ( len > SHMLOG_MSG_BODY_SIZE ) {
        len = SHMLOG_MSG_BODY_SIZE;
    }
    uint32_t mine = atomic_fetch_add(&g_hdr->next, 1);
    mine %= g_msg_cnt;
    g_msgs[mine].hdr.len = 0;
    memcpy(g_msgs[mine].body, data, len);
    if ( len < SHMLOG_MSG_BODY_SIZE ) {
        memset(g_msgs[mine].body+len, 0, SHMLOG_MSG_BODY_SIZE-len);
    }
    g_msgs[mine].hdr.len = len;
    return 0;
}
