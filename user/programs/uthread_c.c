#include <os64/os64.h>

#define THREAD_TEST_WORKERS 7u

static volatile uint32_t worker_counts[THREAD_TEST_WORKERS];
static volatile uint32_t worker_identity_ok[THREAD_TEST_WORKERS];

static long thread_worker(void* argument) {
    uint32_t index = (uint32_t)(uintptr_t)argument;
    OsThreadIdentity self;
    if (index < THREAD_TEST_WORKERS && os_thread_self(&self) == OS_OK &&
        self.tid != 0 && self.generation != 0) {
        worker_identity_ok[index] = 1;
    }
    for (uint32_t i = 0; i < 12; i++) {
        worker_counts[index]++;
        if ((i & 1u) == 0) {
            os_thread_yield();
        } else {
            os_thread_sleep(1);
        }
    }
    return (long)(0x40u + index);
}

int main(void) {
    OsThreadIdentity main_identity;
    OsThreadIdentity workers[THREAD_TEST_WORKERS];
    uint32_t statuses[THREAD_TEST_WORKERS] = {0};
    uint32_t first_counts[3] = {0, 0, 0};
    uint32_t failures = 0;

    if (os_thread_self(&main_identity) != OS_OK ||
        main_identity.tid == 0 || main_identity.generation == 0) {
        failures++;
    }
    if (os_thread_join(main_identity, 0) != OS_ERR_INVALID_ARGUMENT) {
        failures++;
    }

    for (uint32_t i = 0; i < THREAD_TEST_WORKERS; i++) {
        if (os_thread_create(thread_worker,
                             (void*)(uintptr_t)i,
                             OS_THREAD_STACK_DEFAULT,
                             &workers[i]) != OS_OK) {
            failures++;
            workers[i].tid = 0;
            workers[i].generation = 0;
        }
    }

    OsThreadIdentity overflow;
    if (os_thread_create(thread_worker, 0, OS_THREAD_STACK_DEFAULT, &overflow) !=
        OS_ERR_NO_RESOURCES) {
        failures++;
    }

    for (uint32_t i = 0; i < THREAD_TEST_WORKERS; i++) {
        if (workers[i].tid == 0 ||
            os_thread_join(workers[i], &statuses[i]) != OS_OK) {
            failures++;
            continue;
        }
        if (statuses[i] != 0x40u + i || worker_counts[i] != 12u ||
            worker_identity_ok[i] != 1u) {
            failures++;
        }
        if (i < 3) first_counts[i] = worker_counts[i];
        if (os_thread_join(workers[i], 0) != OS_ERR_NOT_FOUND) {
            failures++;
        }
    }

    OsThreadIdentity reused;
    uint32_t reused_status = 0;
    if (os_thread_create(thread_worker,
                         0,
                         OS_THREAD_STACK_DEFAULT,
                         &reused) != OS_OK ||
        reused.generation == 0 ||
        os_thread_join(reused, &reused_status) != OS_OK ||
        reused_status != 0x40u) {
        failures++;
    }

    os_printf("[THREAD] main=%u:%u counts=%u,%u,%u status=%u,%u,%u failures=%u\n",
              main_identity.tid,
              main_identity.generation,
              first_counts[0],
              first_counts[1],
              first_counts[2],
              statuses[0],
              statuses[1],
              statuses[2],
              failures);
    if (failures == 0) {
        os_puts("[THREAD] PASS\n");
        return 0;
    }
    os_puts("[THREAD] FAIL\n");
    return 1;
}
