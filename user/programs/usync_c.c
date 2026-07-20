#include <os64/os64.h>

static OsMutex counter_mutex;
static volatile uint32_t counter;
static OsSemaphore gate;
static OsSemaphore ready;
static OsCondition condition;
static volatile uint32_t condition_ready;
static OsOnce once_control;
static volatile uint32_t once_count;
static volatile uint32_t once_attempts;

static long counter_worker(void* argument) {
    (void)argument;
    for (uint32_t i = 0; i < 40; i++) {
        if (os_mutex_lock(counter_mutex, OS_SYNC_WAIT_FOREVER) != OS_OK) {
            return 1;
        }
        uint32_t value = counter;
        os_thread_yield();
        counter = value + 1;
        if (os_mutex_unlock(counter_mutex) != OS_OK) {
            return 2;
        }
    }
    return 0;
}

static long semaphore_worker(void* argument) {
    (void)argument;
    if (os_semaphore_post(ready, 1) != OS_OK) {
        return 3;
    }
    return os_semaphore_wait(gate, OS_SYNC_WAIT_FOREVER);
}

static long condition_worker(void* argument) {
    (void)argument;
    if (os_mutex_lock(counter_mutex, OS_SYNC_WAIT_FOREVER) != OS_OK) {
        return 4;
    }
    if (os_semaphore_post(ready, 1) != OS_OK) {
        os_mutex_unlock(counter_mutex);
        return 5;
    }
    while (!condition_ready) {
        long result = os_condition_wait(condition, counter_mutex,
                                        OS_SYNC_WAIT_FOREVER);
        if (result != OS_OK) {
            os_mutex_unlock(counter_mutex);
            return 6;
        }
    }
    return os_mutex_unlock(counter_mutex);
}

static long owner_exit_worker(void* argument) {
    (void)argument;
    return os_mutex_lock(counter_mutex, OS_SYNC_WAIT_FOREVER);
}

static long nonowner_unlock_worker(void* argument) {
    (void)argument;
    return os_mutex_unlock(counter_mutex);
}

static long once_initializer(void* argument) {
    (void)argument;
    once_attempts++;
    if (once_attempts == 1) {
        return OS_ERR_IO;
    }
    once_count++;
    os_thread_yield();
    return OS_OK;
}

static long once_worker(void* argument) {
    (void)argument;
    return os_once_run(&once_control, once_initializer, 0);
}

static int join_ok(OsThreadIdentity identity) {
    uint32_t status = 0xFFFFFFFFu;
    return os_thread_join(identity, &status) == OS_OK && status == 0;
}

int main(void) {
    uint32_t failures = 0;
    OsThreadIdentity workers[3];

    if (os_mutex_create(&counter_mutex) != OS_OK ||
        os_semaphore_create(0, 3, &gate) != OS_OK ||
        os_semaphore_create(0, 3, &ready) != OS_OK ||
        os_condition_create(&condition) != OS_OK ||
        os_once_init(&once_control) != OS_OK) {
        os_puts("[SYNC] setup failed\n");
        return 1;
    }

    OsMutex exhausted[27];
    for (uint32_t i = 0; i < 27; i++) {
        if (os_mutex_create(&exhausted[i]) != OS_OK) failures++;
    }
    OsMutex overflow = 0;
    if (os_mutex_create(&overflow) != OS_ERR_NO_RESOURCES) failures++;
    for (uint32_t i = 0; i < 27; i++) {
        if (os_mutex_destroy(exhausted[i]) != OS_OK) failures++;
    }
    if (os_mutex_create(&overflow) != OS_OK ||
        os_mutex_destroy(overflow) != OS_OK) failures++;

    if (os_once_run(&once_control, once_initializer, 0) != OS_ERR_IO) failures++;
    for (uint32_t i = 0; i < 3; i++) {
        if (os_thread_create(counter_worker, 0, OS_THREAD_STACK_DEFAULT,
                             &workers[i]) != OS_OK) {
            failures++;
        }
    }
    for (uint32_t i = 0; i < 3; i++) {
        if (!join_ok(workers[i])) failures++;
    }
    if (counter != 120) failures++;
    os_printf("[SYNC] mutex failures=%u\n", failures);

    for (uint32_t i = 0; i < 2; i++) {
        if (os_thread_create(semaphore_worker, 0, OS_THREAD_STACK_DEFAULT,
                             &workers[i]) != OS_OK) failures++;
    }
    for (uint32_t i = 0; i < 2; i++) {
        if (os_semaphore_wait(ready, OS_SYNC_WAIT_FOREVER) != OS_OK) failures++;
    }
    if (os_semaphore_post(gate, 2) != OS_OK) failures++;
    for (uint32_t i = 0; i < 2; i++) {
        if (!join_ok(workers[i])) failures++;
    }
    if (os_semaphore_post(gate, 3) != OS_OK ||
        os_semaphore_post(gate, 1) != OS_ERR_OUT_OF_RANGE) failures++;
    for (uint32_t i = 0; i < 3; i++) {
        if (os_semaphore_wait(gate, OS_SYNC_WAIT_FOREVER) != OS_OK) failures++;
    }
    if (os_semaphore_wait(gate, 2) != OS_ERR_TIMEOUT) failures++;
    if (os_thread_create(semaphore_worker, 0, OS_THREAD_STACK_DEFAULT,
                         &workers[0]) != OS_OK ||
        os_semaphore_wait(ready, OS_SYNC_WAIT_FOREVER) != OS_OK ||
        os_semaphore_destroy(gate) != OS_OK) failures++;
    uint32_t cancelled_status = 0;
    if (os_thread_join(workers[0], &cancelled_status) != OS_OK ||
        cancelled_status != (uint32_t)OS_ERR_CANCELLED) failures++;
    gate = 0;
    os_printf("[SYNC] semaphore failures=%u\n", failures);

    condition_ready = 0;
    for (uint32_t i = 0; i < 2; i++) {
        if (os_thread_create(condition_worker, 0, OS_THREAD_STACK_DEFAULT,
                             &workers[i]) != OS_OK) failures++;
    }
    for (uint32_t i = 0; i < 2; i++) {
        if (os_semaphore_wait(ready, OS_SYNC_WAIT_FOREVER) != OS_OK) failures++;
    }
    if (os_mutex_lock(counter_mutex, OS_SYNC_WAIT_FOREVER) != OS_OK) failures++;
    condition_ready = 1;
    if (os_condition_broadcast(condition) != OS_OK) failures++;
    if (os_mutex_unlock(counter_mutex) != OS_OK) failures++;
    for (uint32_t i = 0; i < 2; i++) {
        if (!join_ok(workers[i])) failures++;
    }
    if (os_mutex_lock(counter_mutex, OS_SYNC_WAIT_FOREVER) != OS_OK ||
        os_condition_wait(condition, counter_mutex, 2) != OS_ERR_TIMEOUT ||
        os_mutex_unlock(counter_mutex) != OS_OK) failures++;
    os_printf("[SYNC] condition failures=%u\n", failures);

    if (os_mutex_lock(counter_mutex, OS_SYNC_WAIT_FOREVER) != OS_OK ||
        os_mutex_lock(counter_mutex, OS_SYNC_WAIT_FOREVER) != OS_ERR_ALREADY_EXISTS ||
        os_thread_create(nonowner_unlock_worker, 0, OS_THREAD_STACK_DEFAULT,
                         &workers[0]) != OS_OK) failures++;
    uint32_t nonowner_status = 0;
    if (os_thread_join(workers[0], &nonowner_status) != OS_OK ||
        nonowner_status != (uint32_t)OS_ERR_PERMISSION_DENIED ||
        os_mutex_unlock(counter_mutex) != OS_OK) failures++;

    if (os_thread_create(owner_exit_worker, 0, OS_THREAD_STACK_DEFAULT,
                         &workers[0]) != OS_OK || !join_ok(workers[0])) failures++;
    if (os_mutex_lock(counter_mutex, OS_SYNC_WAIT_FOREVER) != OS_OK ||
        os_mutex_unlock(counter_mutex) != OS_OK) failures++;
    os_printf("[SYNC] owner-exit failures=%u\n", failures);

    for (uint32_t i = 0; i < 3; i++) {
        if (os_thread_create(once_worker, 0, OS_THREAD_STACK_DEFAULT,
                             &workers[i]) != OS_OK) failures++;
    }
    for (uint32_t i = 0; i < 3; i++) {
        if (!join_ok(workers[i])) failures++;
    }
    if (once_count != 1 || once_attempts != 2) failures++;
    os_printf("[SYNC] once failures=%u\n", failures);

    if (os_once_destroy(&once_control) != OS_OK ||
        os_condition_destroy(condition) != OS_OK ||
        os_semaphore_destroy(ready) != OS_OK ||
        os_mutex_destroy(counter_mutex) != OS_OK) failures++;

    os_printf("[SYNC] counter=%u once=%u failures=%u\n",
              counter, once_count, failures);
    os_puts(failures == 0 ? "[SYNC] PASS\n" : "[SYNC] FAIL\n");
    return failures == 0 ? 0 : 1;
}
