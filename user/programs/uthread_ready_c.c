#include <os64/os64.h>

static volatile uint64_t tls_cells[4];
static volatile uint64_t work_counts[3];
static volatile uint32_t tls_ok[3];
static OsThreadInfo worker_info[3];

static uint64_t read_fs_zero(void) {
    uint64_t value;
    __asm__ volatile("movq %%fs:0, %0" : "=r"(value));
    return value;
}

static long readiness_worker(void* argument) {
    uint32_t index = (uint32_t)(uintptr_t)argument;
    OsThreadIdentity self;
    void* tls_base = 0;
    tls_cells[index + 1] = 0x544C530000000000ULL + index;
    if (os_thread_self(&self) != OS_OK ||
        os_thread_tls_set((void*)&tls_cells[index + 1]) != OS_OK ||
        os_thread_tls_get(&tls_base) != OS_OK ||
        tls_base != (void*)&tls_cells[index + 1] ||
        read_fs_zero() != tls_cells[index + 1]) {
        return 1;
    }

    uint64_t deadline = os_time_ticks() + 30;
    while (os_time_ticks() < deadline) {
        work_counts[index]++;
    }
    if (read_fs_zero() == tls_cells[index + 1]) {
        tls_ok[index] = 1;
    }
    if (os_thread_get_info(self, &worker_info[index]) != OS_OK) {
        return 2;
    }
    return 0;
}

int main(void) {
    uint32_t failures = 0;
    OsThreadIdentity main_identity;
    OsThreadIdentity workers[3];
    OsThreadInfo before;
    OsThreadInfo after;
    void* tls_base = 0;

    tls_cells[0] = 0x4D41494E544C5300ULL;
    if (os_thread_self(&main_identity) != OS_OK ||
        os_thread_tls_set((void*)&tls_cells[0]) != OS_OK ||
        os_thread_tls_get(&tls_base) != OS_OK ||
        tls_base != (void*)&tls_cells[0] || read_fs_zero() != tls_cells[0] ||
        os_thread_tls_set((void*)0xFFFF800000000000ULL) != OS_ERR_BAD_BUFFER ||
        os_thread_get_info(main_identity, &before) != OS_OK) {
        failures++;
    }

    for (uint32_t i = 0; i < 3; i++) {
        if (os_thread_create(readiness_worker, (void*)(uintptr_t)i,
                             OS_THREAD_STACK_DEFAULT, &workers[i]) != OS_OK ||
            os_thread_set_priority(workers[i], i) != OS_OK) {
            failures++;
        }
    }
    for (uint32_t i = 0; i < 3; i++) {
        uint32_t status = 0xFFFFFFFFu;
        if (os_thread_join(workers[i], &status) != OS_OK || status != 0 ||
            work_counts[i] == 0 || tls_ok[i] != 1 ||
            worker_info[i].priority != i ||
            worker_info[i].runtime_ticks == 0 ||
            worker_info[i].switch_count == 0 ||
            worker_info[i].tls_base != (uint64_t)(uintptr_t)&tls_cells[i + 1]) {
            failures++;
        }
    }

    uint64_t minimum = work_counts[0];
    uint64_t maximum = work_counts[0];
    for (uint32_t i = 1; i < 3; i++) {
        if (work_counts[i] < minimum) minimum = work_counts[i];
        if (work_counts[i] > maximum) maximum = work_counts[i];
    }
    if (minimum == 0 || maximum > minimum * 6u) failures++;

    os_thread_yield();
    os_thread_sleep(2);
    if (os_thread_get_info(main_identity, &after) != OS_OK ||
        after.runtime_ticks < before.runtime_ticks ||
        after.yield_count <= before.yield_count ||
        after.block_count <= before.block_count ||
        after.wake_count <= before.wake_count ||
        after.switch_count < before.switch_count ||
        read_fs_zero() != tls_cells[0]) {
        failures++;
    }

    os_printf("[READY] work=%u,%u,%u preempt=%u,%u,%u failures=%u\n",
              (uint32_t)work_counts[0], (uint32_t)work_counts[1],
              (uint32_t)work_counts[2],
              (uint32_t)worker_info[0].preemption_count,
              (uint32_t)worker_info[1].preemption_count,
              (uint32_t)worker_info[2].preemption_count,
              failures);
    os_puts(failures == 0 ? "[READY] PASS\n" : "[READY] FAIL\n");
    return failures == 0 ? 0 : 1;
}
