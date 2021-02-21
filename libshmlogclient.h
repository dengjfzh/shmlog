#ifndef __DENGJFZH_SHMLOGCLIENT_H__
#define __DENGJFZH_SHMLOGCLIENT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include "libshmlog.h"

struct shm_log_client_t {
    pid_t pid;
    size_t size;
    struct shmlog_header *hdr;
    struct shmlog_msg *msgs;
    shmlog_int_head last_head;
    int nonblock;
    pid_t pid_self;
    shmlog_int_head remain; // the number of remaining message in buffer after reading
};

int shmlogclient_init(pid_t pid, struct shm_log_client_t *client, int nonblock);
void shmlogclient_uninit(struct shm_log_client_t *client);
int shmlogclient_read(struct shm_log_client_t *client, void *buf, size_t size, size_t *lost, int timeout_us);

// zero-copy read (return buffer address)
int shmlogclient_zerocopy_read(struct shm_log_client_t *client, void **pbuf, size_t *plen, size_t *lost, int timeout_us); // return buffer id on success or -1 on error
int shmlogclient_zerocopy_free(struct shm_log_client_t *client, shmlog_int_headtail bufid);
    
#ifdef __cplusplus
}
#endif
    
#endif/*__DENGJFZH_SHMLOGCLIENT_H__*/
