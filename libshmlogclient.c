#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "libshmlogclient.h"

int shmlogclient_init(pid_t pid, struct shm_log_client_t *client)
{
    char filename[256];
    struct stat statbuf;
    int errbak, fd;
    struct shmlog_header *hdr;
    
    if ( NULL == client ) {
        errno = EINVAL;
        return -1;
    }

    // open shm
    snprintf(filename, sizeof(filename), "/dev/shm/" SHM_FILE_PREFIX "%d", pid);
    if ( stat(filename, &statbuf) < 0 )
        return -1;
    if ( statbuf.st_size < sizeof(struct shmlog_fullheader) ) {
        errno = ENOMEM;
        return -1;
    }
    snprintf(filename, sizeof(filename), SHM_FILE_PREFIX "%d", pid);
    fd = shm_open(filename, O_RDONLY, 0666);
    if ( fd < 0 )
        return -1;
    hdr = (struct shmlog_header *)mmap(NULL, statbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);
    if ( NULL == hdr ) {
        errbak = errno;
        close(fd);
        errno = errbak;
        return -1;
    }
    close(fd);

    // check ring buffer
    if ( (sizeof(struct shmlog_fullheader) + hdr->nmsg * sizeof(struct shmlog_msg)) > statbuf.st_size ) {
        munmap((void*)hdr, statbuf.st_size);
        errno = ENOMEM;
        return -1;
    }

    client->pid = pid;
    client->size = statbuf.st_size;
    client->hdr = hdr;
    client->msgs = (struct shmlog_msg *)((uint8_t*)hdr + sizeof(struct shmlog_fullheader));
    client->last = atomic_load(&hdr->next);
    return 0;
}

void shmlogclient_uninit(struct shm_log_client_t *client)
{
    struct shmlog_header *hdr;
    if ( NULL != client ) {
        hdr = client->hdr;
        client->hdr = NULL;
        client->msgs = NULL;
        munmap((void*)hdr, client->size);
    }
}

int shmlogclient_read(struct shm_log_client_t *client, void *buf, size_t size, size_t *lost)
{
    struct shmlog_header *hdr;
    uint32_t nmsg, next, last, cnt, idx, len;
    if ( NULL == client ) {
        errno = EINVAL;
        return -1;
    }
    hdr = client->hdr;
    nmsg = hdr->nmsg;
    next = atomic_load(&hdr->next);
    last = client->last;
    if ( next == last )
        return 0;
    cnt = next - last;
    if ( cnt > nmsg ) {
        if ( NULL != lost )
            *lost = cnt - nmsg;
        last = next - nmsg;
    } else {
        if ( NULL != lost )
            *lost = 0;
    }
    idx = last % nmsg;
    len = (client->msgs[idx].hdr.len < size) ? client->msgs[idx].hdr.len : size;
    memcpy(buf, client->msgs[idx].body, len);
    if ( len < size )
        *((uint8_t*)buf + len) = 0;
    client->last = last + 1;
    return len;
}
