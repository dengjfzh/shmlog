#ifndef __DENGJFZH_SHMLOG_H__
#define __DENGJFZH_SHMLOG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>

    
#define SHM_FILE_PREFIX "dengjfzh-shmlog-"
#define SHMLOG_MSG_SIZE_LOG2 8
#define SHMLOG_MSG_SIZE (1<<SHMLOG_MSG_SIZE_LOG2)

/*
 * Note: C11 atomic in shared memory
 * Operations that are lock-free should also be address-free. That is, atomic
 * operations on the same memory location via two different addresses will
 * communicate atomically. The implementation  should not depend on any
 * per-process state. This restriction enables communication via memory mapped
 * into a process more than once and memory shared between two processes.
 * ISO/IEC 9899:201x 7.17.5 Lock-free property
 */

#if ATOMIC_LLONG_LOCK_FREE == 2
    #define LIBSHMLOG_ATOMIC_SIZE 64
    #define GET_HEAD(ht) ((uint32_t)(((uint64_t)(ht))>>32))
    #define GET_TAIL(ht) ((uint32_t)(ht))
    #define MAKE_HT(head, tail) ((((uint64_t)(head))<<32)+(uint32_t)(tail))
    typedef atomic_ullong atomic_headtail;
    typedef uint64_t int_headtail;
#elif ATOMIC_LONG_LOCK_FREE == 2
    #define LIBSHMLOG_ATOMIC_SIZE 32
    #define GET_HEAD(ht) ((uint16_t)(((uint32_t)(ht))>>16))
    #define GET_TAIL(ht) ((uint16_t)(ht))
    #define MAKE_HT(head, tail) ((((uint32_t)(head))<<16)+(uint16_t)(tail))
    typedef atomic_ulong atomic_headtail;
    typedef uint32_t int_headtail;
#else
    #error atomic_ulong is not lock-free!
#endif

struct shmlog_header {
    uint32_t nmsg;

    atomic_int consumer_pid; // consumer pid decides how to operate when the queue is full:
                             // if consumer_pid <= 0, the oldest msg will be overwritten immediately,
                             // otherwise `push` will be blocked for a moment (about 150ms) if no message is consumed, then the oldest msg will be overwritten.

    atomic_headtail headtail;
};

struct shmlog_fullheader {
    struct shmlog_header hdr;
    uint8_t reserves[SHMLOG_MSG_SIZE-sizeof(struct shmlog_header)];
};

struct shmlog_msg_header {
    atomic_bool filled; // data readly
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
