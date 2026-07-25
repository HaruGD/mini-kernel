#include <os64/os64.h>

static OsSemaphore start_gate;
static volatile uint32_t ready_count;
static volatile uint32_t active_count;
static volatile uint32_t maximum_active;
static volatile uint32_t observed_cpu_mask;
static volatile uint32_t worker_failures;
static volatile uint64_t worker_preemptions;

static void update_maximum(uint32_t value) {
    uint32_t observed = __atomic_load_n(&maximum_active, __ATOMIC_ACQUIRE);
    while (observed < value &&
           !__atomic_compare_exchange_n(&maximum_active,
                                        &observed,
                                        value,
                                        0,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
    }
}

static long smp_worker(void* argument) {
    const uint32_t index = (uint32_t)(uintptr_t)argument;
    const uint32_t logical_cpu = index + 1u;
    const uint32_t affinity = 1u << logical_cpu;
    OsThreadIdentity self;
    if (os_thread_self(&self) != OS_SUCCESS ||
        os_thread_set_affinity(self, affinity) != OS_SUCCESS) {
        __atomic_add_fetch(&worker_failures, 1u, __ATOMIC_RELAXED);
        return 0xE0u + index;
    }

    __atomic_add_fetch(&ready_count, 1u, __ATOMIC_RELEASE);
    if (os_semaphore_wait(start_gate, OS_SYNC_WAIT_FOREVER) != OS_SUCCESS) {
        __atomic_add_fetch(&worker_failures, 1u, __ATOMIC_RELAXED);
        return 0xE4u + index;
    }

    OsThreadInfo before;
    if (os_thread_get_info(self, &before) != OS_SUCCESS ||
        before.running_cpu != (int32_t)logical_cpu ||
        before.affinity_mask != affinity) {
        __atomic_add_fetch(&worker_failures, 1u, __ATOMIC_RELAXED);
    }
    if (before.running_cpu >= 0 && before.running_cpu < 8) {
        __atomic_fetch_or(&observed_cpu_mask,
                          1u << (uint32_t)before.running_cpu,
                          __ATOMIC_ACQ_REL);
    }
    const uint32_t active =
        __atomic_add_fetch(&active_count, 1u, __ATOMIC_ACQ_REL);
    update_maximum(active);

    volatile uint32_t spin = 0x02000000u;
    while (spin-- != 0u) {
        __asm__ volatile("" : : : "memory");
    }

    __atomic_sub_fetch(&active_count, 1u, __ATOMIC_ACQ_REL);
    OsThreadInfo after;
    if (os_thread_get_info(self, &after) != OS_SUCCESS ||
        after.running_cpu != (int32_t)logical_cpu ||
        after.preemption_count == 0) {
        __atomic_add_fetch(&worker_failures, 1u, __ATOMIC_RELAXED);
    } else {
        __atomic_add_fetch(&worker_preemptions,
                           after.preemption_count,
                           __ATOMIC_RELAXED);
    }
    return 0x70u + index;
}

int main(void) {
    OsThreadIdentity workers[3];
    uint32_t failures = 0;
    if (os_semaphore_create(0, 3, &start_gate) != OS_SUCCESS) {
        os_puts("[SMPX] FAIL semaphore\n");
        return 1;
    }
    for (uint32_t i = 0; i < 3; i++) {
        if (os_thread_create(smp_worker,
                             (void*)(uintptr_t)i,
                             OS_THREAD_STACK_DEFAULT,
                             &workers[i]) != OS_SUCCESS) {
            failures++;
        }
    }
    while (__atomic_load_n(&ready_count, __ATOMIC_ACQUIRE) != 3u) {
        os_thread_yield();
    }
    if (os_semaphore_post(start_gate, 3) != OS_SUCCESS) {
        failures++;
    }
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t status = 0;
        if (os_thread_join(workers[i], &status) != OS_SUCCESS ||
            status != 0x70u + i) {
            failures++;
        }
    }
    if (os_semaphore_destroy(start_gate) != OS_SUCCESS) {
        failures++;
    }
    failures += __atomic_load_n(&worker_failures, __ATOMIC_ACQUIRE);
    const uint32_t cpu_mask =
        __atomic_load_n(&observed_cpu_mask, __ATOMIC_ACQUIRE);
    const uint32_t maximum =
        __atomic_load_n(&maximum_active, __ATOMIC_ACQUIRE);
    const uint64_t preemptions =
        __atomic_load_n(&worker_preemptions, __ATOMIC_ACQUIRE);
    os_printf("[SMPX] mask=%u max_active=%u preemptions=%u failures=%u\n",
              cpu_mask,
              maximum,
              (uint32_t)preemptions,
              failures);
    if (failures == 0 && cpu_mask == 0x0Eu &&
        maximum >= 3u && preemptions >= 3u) {
        os_puts("[SMPX] PASS\n");
        return 0;
    }
    os_puts("[SMPX] FAIL\n");
    return 1;
}
