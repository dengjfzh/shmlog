#ifndef __DENGJFZH_SHMLOG_H__
#define __DENGJFZH_SHMLOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdatomic.h>

#if ATOMIC_INT_LOCK_FREE == 0
#error atomic_int is never lock-free!
#elif ATOMIC_INT_LOCK_FREE == 1
#error atomic_int is sometimes lock-free!
#elif ATOMIC_INT_LOCK_FREE == 2
    // #pragma message("atomic_int is always lock-free.")
#endif
    
#define SHM_FILE_PREFIX "shmlog-"
#define SHMLOG_MSG_SIZE_LOG2 8
#define SHMLOG_MSG_SIZE (1<<SHMLOG_MSG_SIZE_LOG2)

struct shmlog_header {
    int size;
    atomic_int next;
};

struct shmlog_fullheader {
    struct shmlog_header hdr;
    uint8_t reserves[SHMLOG_MSG_SIZE-sizeof(struct shmlog_header)];
};

struct shmlog_msg_header {
#if SHMLOG_MSG_SIZE_LOG2 <= 8
    uint8_t len;
#elif SHMLOG_MSG_SIZE_LOG2 <= 16
    uint16_t len;
#else
    #error shmlog message size is too large!
#endif
};

#define SHMLOG_MSG_BODY_SIZE (SHMLOG_MSG_SIZE-sizeof(struct shmlog_msg_header))

struct shmlog_msg {
    struct shmlog_msg_header hdr;
    uint8_t body[SHMLOG_MSG_BODY_SIZE];
};

    
int shmlog_init(size_t nmsg);
void shmlog_uninit();
int shmlog_write(const void *data, size_t len);

#ifdef __cplusplus
}
#endif
    
#endif/*__DENGJFZH_SHMLOG_H__*/
