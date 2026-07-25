#include "kernel/sync/thread_sync.h"

#include "kernel/process64.h"
#include "kernel/fault_injection.h"
#include "kernel/spinlock.h"
#include "kernel/syscall64.h"
#include "os64/sync_types.h"

struct KernelSyncObject {
    uint8_t active;
    uint8_t type;
    uint16_t reserved;
    uint32_t generation;
    ProcessIdentity owner;
    ThreadIdentity mutex_owner;
    uint32_t semaphore_count;
    uint32_t semaphore_maximum;
};

static KernelSyncObject sync_objects[KERNEL_SYNC_MAX_OBJECTS];
static KernelSpinlock sync_lock =
    KERNEL_SPINLOCK_INITIALIZER(KERNEL_LOCK_CLASS_IPC_SERVICE, "thread_sync");

static uint64_t make_object_id(uint32_t index, uint32_t generation) {
    return ((uint64_t)generation << 32) | (uint64_t)(index + 1u);
}

static uint32_t next_generation(uint32_t generation) {
    generation++;
    return generation == 0 ? 1u : generation;
}

static KernelSyncObject* object_from_id(uint64_t object_id, uint32_t type) {
    uint32_t encoded_index = (uint32_t)object_id;
    uint32_t generation = (uint32_t)(object_id >> 32);
    if (encoded_index == 0 || encoded_index > KERNEL_SYNC_MAX_OBJECTS ||
        generation == 0) {
        return 0;
    }
    KernelSyncObject* object = &sync_objects[encoded_index - 1u];
    if (!object->active || object->generation != generation ||
        (type != KERNEL_HANDLE_TYPE_NONE && object->type != type)) {
        return 0;
    }
    return object;
}

static int resolve_object(Process* process,
                          uint64_t handle,
                          uint32_t type,
                          uint64_t* object_id) {
    KernelHandle resolved;
    if (process == 0 || object_id == 0 ||
        !kernel_handle_resolve_copy(&process->handle_table,
                                    handle,
                                    type,
                                    0,
                                    &resolved)) {
        return 0;
    }
    *object_id = resolved.object;
    return 1;
}

static uint64_t create_object(Process* process,
                              uint32_t type,
                              uint32_t initial_count,
                              uint32_t maximum_count) {
    if (process == 0) {
        return 0;
    }
    if (kernel_fault_injection_should_fail(KERNEL_FAULT_POINT_SYNC_OBJECT)) {
        return 0;
    }
    uint32_t index = KERNEL_SYNC_MAX_OBJECTS;
    uint64_t object_id = 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&sync_lock, &token)) {
        return 0;
    }
    for (uint32_t i = 0; i < KERNEL_SYNC_MAX_OBJECTS; i++) {
        if (!sync_objects[i].active) {
            KernelSyncObject* object = &sync_objects[i];
            object->active = 1;
            object->type = (uint8_t)type;
            object->generation = next_generation(object->generation);
            object->owner = process_identity(process);
            object->mutex_owner.tid = 0;
            object->mutex_owner.generation = 0;
            object->semaphore_count = initial_count;
            object->semaphore_maximum = maximum_count;
            index = i;
            object_id = make_object_id(i, object->generation);
            break;
        }
    }
    kernel_spinlock_release(&sync_lock, &token);
    if (index == KERNEL_SYNC_MAX_OBJECTS) {
        return 0;
    }
    uint64_t handle = kernel_handle_alloc(&process->handle_table,
                                          type,
                                          0,
                                          object_id,
                                          0);
    if (handle != 0) {
        return handle;
    }
    if (kernel_spinlock_acquire(&sync_lock, &token)) {
        KernelSyncObject* object = object_from_id(object_id, type);
        if (object != 0) {
            object->active = 0;
        }
        kernel_spinlock_release(&sync_lock, &token);
    }
    return 0;
}

void kernel_sync_init() {
    kernel_spinlock_init(&sync_lock, KERNEL_LOCK_CLASS_IPC_SERVICE, "thread_sync");
    for (uint32_t i = 0; i < KERNEL_SYNC_MAX_OBJECTS; i++) {
        sync_objects[i].active = 0;
        sync_objects[i].type = KERNEL_HANDLE_TYPE_NONE;
        sync_objects[i].generation = 0;
        sync_objects[i].owner.pid = 0;
        sync_objects[i].owner.generation = 0;
        sync_objects[i].mutex_owner.tid = 0;
        sync_objects[i].mutex_owner.generation = 0;
        sync_objects[i].semaphore_count = 0;
        sync_objects[i].semaphore_maximum = 0;
    }
}

uint64_t kernel_mutex_create(Process* process) {
    return create_object(process, KERNEL_HANDLE_TYPE_MUTEX, 0, 0);
}

static int mutex_try_acquire(uint64_t object_id,
                             ProcessIdentity process_identity_value,
                             ThreadIdentity identity,
                             int32_t* error) {
    int acquired = 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&sync_lock, &token)) {
        *error = SYS_ERR_NOT_READY;
        return 0;
    }
    KernelSyncObject* object = object_from_id(object_id, KERNEL_HANDLE_TYPE_MUTEX);
    if (object == 0 || object->owner.pid != process_identity_value.pid ||
        object->owner.generation != process_identity_value.generation) {
        *error = SYS_ERR_NOT_FOUND;
    } else if (object->mutex_owner.tid == identity.tid &&
               object->mutex_owner.generation == identity.generation) {
        *error = SYS_ERR_ALREADY_EXISTS;
    } else if (object->mutex_owner.tid == 0) {
        object->mutex_owner = identity;
        acquired = 1;
        *error = 0;
    }
    kernel_spinlock_release(&sync_lock, &token);
    return acquired;
}

int64_t kernel_mutex_lock(Process* process,
                          Thread* thread,
                          uint64_t handle,
                          uint32_t timeout_ticks,
                          uint32_t tick_now) {
    uint64_t object_id = 0;
    if (thread == 0 || !resolve_object(process, handle,
                                       KERNEL_HANDLE_TYPE_MUTEX, &object_id)) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    int32_t error = SYS_ERR_WOULD_BLOCK;
    if (mutex_try_acquire(object_id, process_identity(process),
                          thread_identity(thread), &error)) {
        return 0;
    }
    if (error != SYS_ERR_WOULD_BLOCK) {
        return error;
    }
    if (!thread_wait_begin(thread, PROCESS_WAIT_MUTEX, 0,
                           timeout_ticks, tick_now) ||
        !thread_wait_set_objects(thread, object_id, 0)) {
        return SYS_ERR_NOT_READY;
    }
    if (mutex_try_acquire(object_id, process_identity(process),
                          thread_identity(thread), &error)) {
        thread_wait_signal(thread, PROCESS_WAIT_MUTEX, PROCESS_WAIT_OK);
    }
    return (int64_t)SYSCALL_WAIT_TO_KERNEL;
}

static void wake_mutex_waiter(Process* process, uint64_t object_id) {
    for (;;) {
        Thread* waiter = thread_wait_find_oldest(process,
                                                 PROCESS_WAIT_MUTEX,
                                                 object_id);
        if (waiter == 0) {
            return;
        }
        int32_t error = SYS_ERR_WOULD_BLOCK;
        if (!mutex_try_acquire(object_id, process_identity(process),
                               thread_identity(waiter), &error)) {
            return;
        }
        int32_t result = waiter->context->wait_result;
        if (thread_wait_signal(waiter, PROCESS_WAIT_MUTEX, result)) {
            return;
        }
        KernelSpinlockToken token;
        if (kernel_spinlock_acquire(&sync_lock, &token)) {
            KernelSyncObject* object = object_from_id(object_id,
                                                      KERNEL_HANDLE_TYPE_MUTEX);
            if (object != 0 && thread_identity_matches(waiter,
                                                       object->mutex_owner)) {
                object->mutex_owner.tid = 0;
                object->mutex_owner.generation = 0;
            }
            kernel_spinlock_release(&sync_lock, &token);
        }
    }
}

int32_t kernel_mutex_unlock(Process* process, Thread* thread, uint64_t handle) {
    uint64_t object_id = 0;
    if (thread == 0 || !resolve_object(process, handle,
                                       KERNEL_HANDLE_TYPE_MUTEX, &object_id)) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&sync_lock, &token)) {
        return SYS_ERR_NOT_READY;
    }
    KernelSyncObject* object = object_from_id(object_id, KERNEL_HANDLE_TYPE_MUTEX);
    ThreadIdentity identity = thread_identity(thread);
    if (object == 0 || object->mutex_owner.tid != identity.tid ||
        object->mutex_owner.generation != identity.generation) {
        kernel_spinlock_release(&sync_lock, &token);
        return SYS_ERR_PERMISSION_DENIED;
    }
    object->mutex_owner.tid = 0;
    object->mutex_owner.generation = 0;
    kernel_spinlock_release(&sync_lock, &token);
    wake_mutex_waiter(process, object_id);
    return 0;
}

uint64_t kernel_semaphore_create(Process* process,
                                 uint32_t initial_count,
                                 uint32_t maximum_count) {
    if (maximum_count == 0 || maximum_count > OS_SEMAPHORE_MAX_COUNT ||
        initial_count > maximum_count) {
        return 0;
    }
    return create_object(process, KERNEL_HANDLE_TYPE_SEMAPHORE,
                         initial_count, maximum_count);
}

static int semaphore_try_take(uint64_t object_id, int32_t* error) {
    int acquired = 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&sync_lock, &token)) {
        *error = SYS_ERR_NOT_READY;
        return 0;
    }
    KernelSyncObject* object = object_from_id(object_id,
                                              KERNEL_HANDLE_TYPE_SEMAPHORE);
    if (object == 0) {
        *error = SYS_ERR_NOT_FOUND;
    } else if (object->semaphore_count != 0) {
        object->semaphore_count--;
        acquired = 1;
        *error = 0;
    }
    kernel_spinlock_release(&sync_lock, &token);
    return acquired;
}

int64_t kernel_semaphore_wait(Process* process,
                              Thread* thread,
                              uint64_t handle,
                              uint32_t timeout_ticks,
                              uint32_t tick_now) {
    uint64_t object_id = 0;
    if (thread == 0 || !resolve_object(process, handle,
                                       KERNEL_HANDLE_TYPE_SEMAPHORE, &object_id)) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    int32_t error = SYS_ERR_WOULD_BLOCK;
    if (semaphore_try_take(object_id, &error)) {
        return 0;
    }
    if (error != SYS_ERR_WOULD_BLOCK) {
        return error;
    }
    if (!thread_wait_begin(thread, PROCESS_WAIT_SEMAPHORE, 0,
                           timeout_ticks, tick_now) ||
        !thread_wait_set_objects(thread, object_id, 0)) {
        return SYS_ERR_NOT_READY;
    }
    if (semaphore_try_take(object_id, &error)) {
        thread_wait_signal(thread, PROCESS_WAIT_SEMAPHORE, PROCESS_WAIT_OK);
    }
    return (int64_t)SYSCALL_WAIT_TO_KERNEL;
}

int32_t kernel_semaphore_post(Process* process,
                              uint64_t handle,
                              uint32_t release_count) {
    uint64_t object_id = 0;
    if (release_count == 0 || !resolve_object(process, handle,
                                               KERNEL_HANDLE_TYPE_SEMAPHORE,
                                               &object_id)) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t released = 0; released < release_count; released++) {
        Thread* waiter = thread_wait_find_oldest(process,
                                                 PROCESS_WAIT_SEMAPHORE,
                                                 object_id);
        if (waiter != 0 && thread_wait_signal(waiter,
                                              PROCESS_WAIT_SEMAPHORE,
                                              PROCESS_WAIT_OK)) {
            continue;
        }
        KernelSpinlockToken token;
        if (!kernel_spinlock_acquire(&sync_lock, &token)) {
            return SYS_ERR_NOT_READY;
        }
        KernelSyncObject* object = object_from_id(object_id,
                                                  KERNEL_HANDLE_TYPE_SEMAPHORE);
        if (object == 0) {
            kernel_spinlock_release(&sync_lock, &token);
            return SYS_ERR_NOT_FOUND;
        }
        if (object->semaphore_count == object->semaphore_maximum) {
            kernel_spinlock_release(&sync_lock, &token);
            return SYS_ERR_OUT_OF_RANGE;
        }
        object->semaphore_count++;
        kernel_spinlock_release(&sync_lock, &token);
    }
    return 0;
}

uint64_t kernel_condition_create(Process* process) {
    return create_object(process, KERNEL_HANDLE_TYPE_CONDITION, 0, 0);
}

static void finish_condition_wait(Thread* waiter, int32_t result) {
    if (waiter == 0 || waiter->context == 0) {
        return;
    }
    uint64_t mutex_id = waiter->context->wait_aux_object_id;
    int32_t error = SYS_ERR_WOULD_BLOCK;
    if (mutex_try_acquire(mutex_id, process_identity(waiter->owner),
                          thread_identity(waiter), &error)) {
        thread_wait_signal(waiter, PROCESS_WAIT_CONDITION, result);
        return;
    }
    if (error == SYS_ERR_WOULD_BLOCK) {
        thread_wait_retarget(waiter,
                             PROCESS_WAIT_CONDITION,
                             PROCESS_WAIT_MUTEX,
                             mutex_id,
                             waiter->context->wait_object_id,
                             result);
    } else {
        thread_wait_signal(waiter, PROCESS_WAIT_CONDITION, error);
    }
}

int64_t kernel_condition_wait(Process* process,
                              Thread* thread,
                              uint64_t condition_handle,
                              uint64_t mutex_handle,
                              uint32_t timeout_ticks,
                              uint32_t tick_now) {
    uint64_t condition_id = 0;
    uint64_t mutex_id = 0;
    if (thread == 0 ||
        !resolve_object(process, condition_handle,
                        KERNEL_HANDLE_TYPE_CONDITION, &condition_id) ||
        !resolve_object(process, mutex_handle,
                        KERNEL_HANDLE_TYPE_MUTEX, &mutex_id)) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    if (!thread_wait_begin(thread, PROCESS_WAIT_CONDITION, 0,
                           timeout_ticks, tick_now) ||
        !thread_wait_set_objects(thread, condition_id, mutex_id)) {
        return SYS_ERR_NOT_READY;
    }
    int32_t unlock_result = kernel_mutex_unlock(process, thread, mutex_handle);
    if (unlock_result != 0) {
        thread_wait_signal(thread, PROCESS_WAIT_CONDITION, unlock_result);
        return (int64_t)SYSCALL_WAIT_TO_KERNEL;
    }
    return (int64_t)SYSCALL_WAIT_TO_KERNEL;
}

int32_t kernel_condition_signal(Process* process,
                                uint64_t condition_handle,
                                int broadcast) {
    uint64_t condition_id = 0;
    if (!resolve_object(process, condition_handle,
                        KERNEL_HANDLE_TYPE_CONDITION, &condition_id)) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    do {
        Thread* waiter = thread_wait_find_oldest(process,
                                                 PROCESS_WAIT_CONDITION,
                                                 condition_id);
        if (waiter == 0) {
            break;
        }
        finish_condition_wait(waiter, PROCESS_WAIT_OK);
    } while (broadcast);
    return 0;
}

int kernel_sync_release_handle_object(const KernelHandle* handle) {
    if (handle == 0 || (handle->type != KERNEL_HANDLE_TYPE_MUTEX &&
                        handle->type != KERNEL_HANDLE_TYPE_SEMAPHORE &&
                        handle->type != KERNEL_HANDLE_TYPE_CONDITION)) {
        return 0;
    }
    ProcessIdentity owner_identity = {0, 0};
    KernelSpinlockToken token;
    if (kernel_spinlock_acquire(&sync_lock, &token)) {
        KernelSyncObject* object = object_from_id(handle->object, handle->type);
        if (object != 0) {
            owner_identity = object->owner;
            object->active = 0;
            object->type = KERNEL_HANDLE_TYPE_NONE;
            object->mutex_owner.tid = 0;
            object->mutex_owner.generation = 0;
            object->semaphore_count = 0;
            object->semaphore_maximum = 0;
        }
        kernel_spinlock_release(&sync_lock, &token);
    }
    Process* owner = find_process_by_identity(owner_identity);
    if (owner != 0) {
        thread_wait_cancel_object(owner, handle->object, PROCESS_WAIT_CANCELLED);
    }
    return 1;
}

extern "C" void kernel_sync_thread_exit(Thread* thread) {
    if (thread == 0 || thread->owner == 0) {
        return;
    }
    uint64_t released[KERNEL_SYNC_MAX_OBJECTS];
    uint32_t released_count = 0;
    ThreadIdentity identity = thread_identity(thread);
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&sync_lock, &token)) {
        return;
    }
    for (uint32_t i = 0; i < KERNEL_SYNC_MAX_OBJECTS; i++) {
        KernelSyncObject* object = &sync_objects[i];
        if (object->active && object->type == KERNEL_HANDLE_TYPE_MUTEX &&
            object->mutex_owner.tid == identity.tid &&
            object->mutex_owner.generation == identity.generation) {
            object->mutex_owner.tid = 0;
            object->mutex_owner.generation = 0;
            released[released_count++] = make_object_id(i, object->generation);
        }
    }
    kernel_spinlock_release(&sync_lock, &token);
    for (uint32_t i = 0; i < released_count; i++) {
        wake_mutex_waiter(thread->owner, released[i]);
    }
}

extern "C" void kernel_sync_timeout_thread(Thread* thread) {
    if (thread == 0 || thread->context == 0 ||
        !thread->context->wait_pending) {
        return;
    }
    uint32_t reason = thread->context->wait_reason;
    if (reason == PROCESS_WAIT_CONDITION) {
        finish_condition_wait(thread, PROCESS_WAIT_TIMEOUT);
    } else if (reason == PROCESS_WAIT_MUTEX ||
               reason == PROCESS_WAIT_SEMAPHORE) {
        thread_wait_signal(thread, reason, PROCESS_WAIT_TIMEOUT);
    }
}
