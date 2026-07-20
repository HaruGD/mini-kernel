#ifndef KERNEL_SYNC_THREAD_SYNC_H
#define KERNEL_SYNC_THREAD_SYNC_H

#include <stdint.h>

#include "kernel/handle/kernel_handle.h"
#include "kernel/thread.h"

struct Process;

#define KERNEL_SYNC_MAX_OBJECTS 32u

void kernel_sync_init();
uint64_t kernel_mutex_create(Process* process);
int64_t kernel_mutex_lock(Process* process,
                          Thread* thread,
                          uint64_t handle,
                          uint32_t timeout_ticks,
                          uint32_t tick_now);
int32_t kernel_mutex_unlock(Process* process, Thread* thread, uint64_t handle);
uint64_t kernel_semaphore_create(Process* process,
                                 uint32_t initial_count,
                                 uint32_t maximum_count);
int64_t kernel_semaphore_wait(Process* process,
                              Thread* thread,
                              uint64_t handle,
                              uint32_t timeout_ticks,
                              uint32_t tick_now);
int32_t kernel_semaphore_post(Process* process,
                              uint64_t handle,
                              uint32_t release_count);
uint64_t kernel_condition_create(Process* process);
int64_t kernel_condition_wait(Process* process,
                              Thread* thread,
                              uint64_t condition_handle,
                              uint64_t mutex_handle,
                              uint32_t timeout_ticks,
                              uint32_t tick_now);
int32_t kernel_condition_signal(Process* process,
                                uint64_t condition_handle,
                                int broadcast);
int kernel_sync_release_handle_object(const KernelHandle* handle);

extern "C" void kernel_sync_thread_exit(Thread* thread);
extern "C" void kernel_sync_timeout_thread(Thread* thread);

#endif
