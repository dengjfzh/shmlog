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
    uint32_t last; // last read position
};

int shmlogclient_init(pid_t pid, struct shm_log_client_t *client);
void shmlogclient_uninit(struct shm_log_client_t *client);
int shmlogclient_read(struct shm_log_client_t *client, void *buf, size_t size, size_t *lost);

#ifdef __cplusplus
}
#endif
    
#endif/*__DENGJFZH_SHMLOGCLIENT_H__*/
