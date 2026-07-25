#include <os64/os64.h>

static OsSemaphore sibling_ready;
static OsSemaphore sibling_hold;
static volatile uint32_t sibling_progress;

static long sibling_worker(void* argument) {
    (void)argument;
    sibling_progress = 1;
    if (os_semaphore_post(sibling_ready, 1) != OS_OK) {
        return 1;
    }
    sibling_progress++;
    return os_semaphore_wait(sibling_hold, OS_SYNC_WAIT_FOREVER);
}

static long fault_worker(void* argument) {
    (void)argument;
    OsThreadIdentity self;
    if (os_semaphore_wait(sibling_ready, OS_SYNC_WAIT_FOREVER) != OS_OK ||
        os_thread_self(&self) != OS_OK) {
        return 2;
    }
    os_printf("[THREAD-FAULT] tid=%u:%u sibling_progress=%u\n",
              self.tid, self.generation, sibling_progress);
    volatile uint64_t* bad = (volatile uint64_t*)0x0000000080000000ULL;
    *bad = 0x4641554C54544852ULL;
    return 3;
}

int main(void) {
    OsThreadIdentity sibling;
    OsThreadIdentity faulting;
    uint32_t status = 0;
    if (os_semaphore_create(0, 1, &sibling_ready) != OS_OK ||
        os_semaphore_create(0, 1, &sibling_hold) != OS_OK ||
        os_thread_create(sibling_worker, 0, OS_THREAD_STACK_DEFAULT,
                         &sibling) != OS_OK ||
        os_thread_create(fault_worker, 0, OS_THREAD_STACK_DEFAULT,
                         &faulting) != OS_OK) {
        os_puts("[THREAD-FAULT] setup failed\n");
        return 1;
    }
    os_thread_join(faulting, &status);
    os_puts("[THREAD-FAULT] process-wide fault policy failed\n");
    return 2;
}
