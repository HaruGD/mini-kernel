#ifndef OS64_THREAD_TYPES_H
#define OS64_THREAD_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define OS64_THREAD_ABI_VERSION 1u
#define OS_THREAD_CREATE_FLAG_NONE 0u
#define OS_THREAD_CREATE_VALID_FLAGS OS_THREAD_CREATE_FLAG_NONE
#define OS_THREAD_STACK_DEFAULT 16384u
#define OS_THREAD_STACK_MIN 4096u
#define OS_THREAD_STACK_MAX 16384u
#define OS_THREAD_PRIORITY_LOW 0u
#define OS_THREAD_PRIORITY_NORMAL 1u
#define OS_THREAD_PRIORITY_HIGH 2u
#define OS_THREAD_PRIORITY_MAX OS_THREAD_PRIORITY_HIGH

typedef struct OsThreadIdentity {
    uint32_t tid;
    uint32_t generation;
} OsThreadIdentity;

typedef long (*OsThreadEntry)(void* argument);

typedef struct OsThreadCreateRequest {
    uint32_t size;
    uint32_t flags;
    uint32_t stack_size;
    uint32_t reserved;
    uint64_t entry;
    uint64_t argument;
    uint64_t return_trampoline;
} OsThreadCreateRequest;

typedef struct OsThreadInfo {
    uint32_t size;
    uint32_t scheduler_state;
    OsThreadIdentity identity;
    uint32_t owner_pid;
    uint32_t owner_generation;
    uint32_t wait_reason;
    uint32_t priority;
    uint32_t timeslice_remaining;
    uint32_t queue_position;
    uint64_t tls_base;
    uint64_t runtime_ticks;
    uint64_t preemption_count;
    uint64_t yield_count;
    uint64_t block_count;
    uint64_t wake_count;
    uint64_t switch_count;
} OsThreadInfo;

#ifdef __cplusplus
#define OS64_THREAD_STATIC_ASSERT(condition, message) static_assert((condition), message)
#else
#define OS64_THREAD_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#endif

OS64_THREAD_STATIC_ASSERT(sizeof(OsThreadIdentity) == 8,
                          "OsThreadIdentity ABI changed");
OS64_THREAD_STATIC_ASSERT(offsetof(OsThreadIdentity, tid) == 0,
                          "OsThreadIdentity.tid offset changed");
OS64_THREAD_STATIC_ASSERT(offsetof(OsThreadIdentity, generation) == 4,
                          "OsThreadIdentity.generation offset changed");
OS64_THREAD_STATIC_ASSERT(sizeof(OsThreadCreateRequest) == 40,
                          "OsThreadCreateRequest ABI changed");
OS64_THREAD_STATIC_ASSERT(offsetof(OsThreadCreateRequest, entry) == 16,
                          "OsThreadCreateRequest.entry offset changed");
OS64_THREAD_STATIC_ASSERT(offsetof(OsThreadCreateRequest, return_trampoline) == 32,
                          "OsThreadCreateRequest.return_trampoline offset changed");
OS64_THREAD_STATIC_ASSERT(sizeof(OsThreadInfo) == 96,
                          "OsThreadInfo ABI changed");
OS64_THREAD_STATIC_ASSERT(offsetof(OsThreadInfo, tls_base) == 40,
                          "OsThreadInfo.tls_base offset changed");

#undef OS64_THREAD_STATIC_ASSERT

#endif
