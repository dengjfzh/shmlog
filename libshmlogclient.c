#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <threads.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "libshmlogclient.h"

#if 1
#define LOG(fmt, arg...) fprintf(stderr, "<%s:%d> " fmt, __FILE__, __LINE__, ##arg)
#else
#define LOG(fmt, arg...)
#endif

#define GET_HEAD(ht) SHMLOG_GET_HEAD(ht)
#define GET_TAIL(ht) SHMLOG_GET_TAIL(ht)
#define MAKE_HT(head, tail) SHMLOG_MAKE_HT(head, tail)
#define INTHEAD_MAX SHMLOG_INTHEAD_MAX


int shmlogclient_init(pid_t pid, struct shm_log_client_t *client, int nonblock)
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
    snprintf(filename, sizeof(filename), SHMLOG_FILE_PREFIX "%d", pid);
    fd = shm_open(filename, O_RDWR, 0666);
    if ( fd < 0 ) {
        LOG("Error: shm_open failed! %d:%s\n", errno, strerror(errno));
        return -1;
    }
    if ( fstat(fd, &statbuf) < 0 ) {
        LOG("Error: fstat failed! %d:%s\n", errno, strerror(errno));
        errbak = errno;
        close(fd);
        errno = errbak;
        return -1;
    }
    if ( statbuf.st_size < sizeof(struct shmlog_fullheader) ) {
        LOG("Error: invalid shm size %lu\n", statbuf.st_size);
        close(fd);
        errno = ENOMEM;
        return -1;
    }
    hdr = (struct shmlog_header *)mmap(NULL, statbuf.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if ( NULL == hdr ) {
        LOG("Error: mmap failed! %d:%s\n", errno, strerror(errno));
        errbak = errno;
        close(fd);
        errno = errbak;
        return -1;
    }
    close(fd);

    // check ring buffer
    if ( (sizeof(struct shmlog_fullheader) + hdr->nmsg * sizeof(struct shmlog_msg)) > statbuf.st_size ) {
        LOG("Error: invalid shm size %lu\n", statbuf.st_size);
        munmap((void*)hdr, statbuf.st_size);
        errno = ENOMEM;
        return -1;
    }

    client->pid = pid;
    client->size = statbuf.st_size;
    client->hdr = hdr;
    client->msgs = (struct shmlog_msg *)((uint8_t*)hdr + sizeof(struct shmlog_fullheader));
    client->last_head = GET_HEAD(atomic_load(&hdr->headtail));
    client->nonblock = nonblock;
    client->pid_self = getpid();
    client->remain = 0;

    if ( !nonblock ) {
        // register consumer if there is no one. this will block producer for a moment if queue is full
        int consumer_pid_old = 0;
        atomic_compare_exchange_strong(&hdr->consumer_pid, &consumer_pid_old, client->pid_self);
    }
    
    return 0;
}

void shmlogclient_uninit(struct shm_log_client_t *client)
{
    struct shmlog_header *hdr;
    if ( NULL != client ) {
        hdr = client->hdr;
        client->hdr = NULL;
        client->msgs = NULL;
        // unregister consumer
        int consumer_pid_old = client->pid_self;
        atomic_compare_exchange_strong(&hdr->consumer_pid, &consumer_pid_old, 0);
        //
        munmap((void*)hdr, client->size);
    }
}

int shmlogclient_read(struct shm_log_client_t *client, void *buf, size_t size, size_t *lost, int timeout_us)
{
    struct shmlog_header *hdr;
    struct shmlog_msg *msgs;
    shmlog_int_headtail ht_old, ht_new;
    shmlog_int_head head, tail, head_new, tail_new;
    bool empty;
    int empty_wait, total_wait, len;
    if ( NULL == client ) {
        errno = EINVAL;
        return -1;
    }
    if ( NULL != lost ) {
        *lost = 0;
    }
    hdr = client->hdr;
    msgs = client->msgs;
    if ( !client->nonblock ) {
        // register consumer if there is no one. this will block producer for a moment if queue is full
        int consumer_pid_old = 0;
        atomic_compare_exchange_strong(&hdr->consumer_pid, &consumer_pid_old, client->pid_self);
    }
    empty_wait = 1;
    total_wait = 0;
    ht_old = atomic_load(&hdr->headtail);
    do {
        head = GET_HEAD(ht_old);
        tail = GET_TAIL(ht_old);
        if ( head > tail ) {
            LOG("[dengjfzh/libshmlogclient] Internal Error: head(%u) > tail(%u)! %s:%d\n", head, tail, __FILE__, __LINE__);
            abort();
        }
        if ( head == tail ) { // empty
            if ( timeout_us >= 0 && total_wait >= timeout_us ) {
                errno = ETIMEDOUT;
                return -1;
            }
            empty = true;
            if ( empty_wait < 65536 ) {
                empty_wait *= 2;
            }
            usleep(empty_wait);
            total_wait += empty_wait;
            ht_old = atomic_load(&hdr->headtail);
            continue;
        }
        empty = false;
        empty_wait = 1;
        head_new = head + 1;
        tail_new = tail;
        if ( head_new >= hdr->nmsg ) {
            shmlog_int_head tmp = (head_new / hdr->nmsg) * hdr->nmsg;
            head_new -= tmp;
            tail_new -= tmp;
        }
        ht_new = MAKE_HT(head_new, tail_new);
    } while ( empty || !atomic_compare_exchange_weak(&hdr->headtail, &ht_old, ht_new) );
    if ( NULL != lost ) {
        *lost = head + ((head<client->last_head) ? hdr->nmsg : 0) - client->last_head;
    }
    client->last_head = head_new;
    client->remain = tail_new - head_new;
    if ( head >= hdr->nmsg ) {
        head %= hdr->nmsg;
    }
    while ( !atomic_load(&msgs[head].hdr.filled) ){
        thrd_yield();
    }
    len = (msgs[head].hdr.len < size) ? (int)(msgs[head].hdr.len) : (int)(size);
    memcpy(buf, msgs[head].body, len);
    atomic_store(&msgs[head].hdr.filled, false);
    return len;
}

int shmlogclient_zerocopy_read(struct shm_log_client_t *client, void **pbuf, size_t *plen, size_t *lost, int timeout_us)
{
    struct shmlog_header *hdr;
    struct shmlog_msg *msgs;
    shmlog_int_headtail ht_old, ht_new;
    shmlog_int_head head, tail, head_new, tail_new;
    bool empty;
    int empty_wait, total_wait;
    if ( NULL == client ) {
        errno = EINVAL;
        return -1;
    }
    if ( NULL != lost ) {
        *lost = 0;
    }
    hdr = client->hdr;
    msgs = client->msgs;
    if ( !client->nonblock ) {
        // register consumer if there is no one. this will block producer for a moment if queue is full
        int consumer_pid_old = 0;
        atomic_compare_exchange_strong(&hdr->consumer_pid, &consumer_pid_old, client->pid_self);
    }
    empty_wait = 1;
    total_wait = 0;
    ht_old = atomic_load(&hdr->headtail);
    do {
        head = GET_HEAD(ht_old);
        tail = GET_TAIL(ht_old);
        if ( head > tail ) {
            LOG("[dengjfzh/libshmlogclient] Internal Error: head(%u) > tail(%u)! %s:%d\n", head, tail, __FILE__, __LINE__);
            abort();
        }
        if ( head == tail ) { // empty
            if ( timeout_us >= 0 && total_wait >= timeout_us ) {
                errno = ETIMEDOUT;
                return -1;
            }
            empty = true;
            if ( empty_wait < 65536 ) {
                empty_wait *= 2;
            }
            usleep(empty_wait);
            total_wait += empty_wait;
            ht_old = atomic_load(&hdr->headtail);
            continue;
        }
        empty = false;
        empty_wait = 1;
        head_new = head + 1;
        tail_new = tail;
        if ( head_new >= hdr->nmsg ) {
            shmlog_int_head tmp = (head_new / hdr->nmsg) * hdr->nmsg;
            head_new -= tmp;
            tail_new -= tmp;
        }
        ht_new = MAKE_HT(head_new, tail_new);
    } while ( empty || !atomic_compare_exchange_weak(&hdr->headtail, &ht_old, ht_new) );
    if ( NULL != lost ) {
        *lost = head + ((head<client->last_head) ? hdr->nmsg : 0) - client->last_head;
    }
    client->last_head = head_new;
    client->remain = tail_new - head_new;
    if ( head >= hdr->nmsg ) {
        head %= hdr->nmsg;
    }
    while ( !atomic_load(&msgs[head].hdr.filled) ){
        thrd_yield();
    }
    if ( NULL != plen )
        *plen = msgs[head].hdr.len;
    if ( NULL != pbuf )
        *pbuf = msgs[head].body;
    return head;
}

int shmlogclient_zerocopy_free(struct shm_log_client_t *client, shmlog_int_headtail bufid)
{
    if ( NULL == client ) {
        errno = EINVAL;
        return -1;
    }
    atomic_store(&client->msgs[bufid].hdr.filled, false);
    return 0;
}
