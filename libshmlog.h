#ifndef __DENGJFZH_SHMLOG_H__
#define __DENGJFZH_SHMLOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
    
#define SHM_FILE_PREFIX "dtrace-shm-"
#define SHMLOG_MSG_SIZE 1024

struct shmlog_header {
    uint16_t size;
    uint16_t next;
};

struct shmlog_fullheader {
    struct shmlog_header hdr;
    char reserves[SHMLOG_MSG_SIZE-sizeof(struct shmlog_header)];
};

struct shmlog_msg_header {
    uint16_t use : 1;
    uint16_t reserve : 5;
    uint16_t len : 10;
};

struct shmlog_msg {
    struct shmlog_msg_header hdr;
    char body[SHMLOG_MSG_SIZE-sizeof(struct shmlog_msg_header)];
};

    
int shmlog_init(size_t nmsg);
void shmlog_uninit();
int shmlog_write(const void *data, size_t len);

#ifdef __cplusplus
}
#endif
    
#endif/*__DENGJFZH_SHMLOG_H__*/
