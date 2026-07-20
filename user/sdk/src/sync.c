#include <os64/os64.h>

#include "internal.h"

static long create_handle(long syscall_number, long arg1, long arg2,
                          OsHandle* handle) {
    if (handle == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    long result = os_syscall2(syscall_number, arg1, arg2);
    if (result < 0) {
        *handle = 0;
        return result;
    }
    *handle = (OsHandle)result;
    return OS_SUCCESS;
}

long os_mutex_create(OsMutex* mutex) {
    return create_handle(OS_SYS_MUTEX_CREATE, 0, 0, mutex);
}

long os_mutex_lock(OsMutex mutex, uint32_t timeout_ticks) {
    return mutex != 0
        ? os_syscall2(OS_SYS_MUTEX_LOCK, (long)mutex, timeout_ticks)
        : OS_ERR_INVALID_ARGUMENT;
}

long os_mutex_unlock(OsMutex mutex) {
    return mutex != 0
        ? os_syscall1(OS_SYS_MUTEX_UNLOCK, (long)mutex)
        : OS_ERR_INVALID_ARGUMENT;
}

long os_mutex_destroy(OsMutex mutex) {
    return os_handle_close(mutex);
}

long os_semaphore_create(uint32_t initial_count,
                         uint32_t maximum_count,
                         OsSemaphore* semaphore) {
    if (maximum_count == 0 || initial_count > maximum_count) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return create_handle(OS_SYS_SEMAPHORE_CREATE, initial_count,
                         maximum_count, semaphore);
}

long os_semaphore_wait(OsSemaphore semaphore, uint32_t timeout_ticks) {
    return semaphore != 0
        ? os_syscall2(OS_SYS_SEMAPHORE_WAIT, (long)semaphore, timeout_ticks)
        : OS_ERR_INVALID_ARGUMENT;
}

long os_semaphore_post(OsSemaphore semaphore, uint32_t release_count) {
    if (semaphore == 0 || release_count == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return os_syscall2(OS_SYS_SEMAPHORE_POST, (long)semaphore, release_count);
}

long os_semaphore_destroy(OsSemaphore semaphore) {
    return os_handle_close(semaphore);
}

long os_condition_create(OsCondition* condition) {
    return create_handle(OS_SYS_CONDITION_CREATE, 0, 0, condition);
}

long os_condition_wait(OsCondition condition,
                       OsMutex mutex,
                       uint32_t timeout_ticks) {
    if (condition == 0 || mutex == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    return os_syscall3(OS_SYS_CONDITION_WAIT, (long)condition,
                       (long)mutex, timeout_ticks);
}

long os_condition_signal(OsCondition condition) {
    return condition != 0
        ? os_syscall1(OS_SYS_CONDITION_SIGNAL, (long)condition)
        : OS_ERR_INVALID_ARGUMENT;
}

long os_condition_broadcast(OsCondition condition) {
    return condition != 0
        ? os_syscall1(OS_SYS_CONDITION_BROADCAST, (long)condition)
        : OS_ERR_INVALID_ARGUMENT;
}

long os_condition_destroy(OsCondition condition) {
    return os_handle_close(condition);
}

long os_once_init(OsOnce* once) {
    if (once == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    once->mutex = 0;
    once->initialized = 0;
    once->complete = 0;
    long result = os_mutex_create(&once->mutex);
    if (result == OS_SUCCESS) {
        once->initialized = 1;
    }
    return result;
}

long os_once_run(OsOnce* once, OsOnceInitializer initializer, void* context) {
    if (once == 0 || initializer == 0 || !once->initialized ||
        once->mutex == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    long result = os_mutex_lock(once->mutex, OS_SYNC_WAIT_FOREVER);
    if (result != OS_SUCCESS) {
        return result;
    }
    if (!once->complete) {
        result = initializer(context);
        if (result == OS_SUCCESS) {
            once->complete = 1;
        }
    }
    long unlock_result = os_mutex_unlock(once->mutex);
    return result == OS_SUCCESS ? unlock_result : result;
}

long os_once_destroy(OsOnce* once) {
    if (once == 0 || !once->initialized || once->mutex == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    long result = os_mutex_destroy(once->mutex);
    if (result == OS_SUCCESS) {
        once->mutex = 0;
        once->initialized = 0;
        once->complete = 0;
    }
    return result;
}
