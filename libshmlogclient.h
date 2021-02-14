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
    int_headtail last_head;
    int nonblock;
    pid_t pid_self;
};

int shmlogclient_init(pid_t pid, struct shm_log_client_t *client, int nonblock);
void shmlogclient_uninit(struct shm_log_client_t *client);
int shmlogclient_read(struct shm_log_client_t *client, void *buf, size_t size, size_t *lost, int timeout_us);

#ifdef __cplusplus
}
#endif
    
#endif/*__DENGJFZH_SHMLOGCLIENT_H__*/
