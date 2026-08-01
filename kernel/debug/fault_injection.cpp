#include "kernel/fault_injection.h"

struct KernelFaultPointState {
    volatile uint64_t attempts;
    volatile uint64_t failures;
    volatile uint64_t remaining;
    volatile uint32_t armed;
};

static KernelFaultPointState fault_points[KERNEL_FAULT_POINT_COUNT];
static const char* fault_names[KERNEL_FAULT_POINT_COUNT] = {
    "pmm", "heap", "process", "mailbox", "service", "handle", "shared",
    "thread_record", "thread_user_stack", "thread_kernel_stack",
    "thread_mapping", "thread_wait", "sync_object", "cpu_local",
    "ap_startup", "local_timer", "scheduler_claim", "tlb_request",
    "irq_owner", "driver_alloc", "driver_va_record", "driver_image_page",
    "driver_alloc_record", "driver_page_run", "driver_mmio_record",
    "driver_page_map", "driver_dma_record", "driver_dma_bounce",
    "driver_dma_domain", "driver_irq_drain", "driver_quiesce"
};

static int text_equal(const char* left, const char* right) {
    if (left == 0 || right == 0) {
        return 0;
    }
    while (*left != '\0' && *right != '\0') {
        if (*left++ != *right++) {
            return 0;
        }
    }
    return *left == '\0' && *right == '\0';
}

void kernel_fault_injection_reset() {
    for (uint32_t i = 0; i < KERNEL_FAULT_POINT_COUNT; i++) {
        __atomic_store_n(&fault_points[i].attempts, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&fault_points[i].failures, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&fault_points[i].remaining, 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&fault_points[i].armed, 0u, __ATOMIC_RELEASE);
    }
}

int kernel_fault_injection_arm(uint32_t point, uint64_t successes_before_failure) {
    if (point >= KERNEL_FAULT_POINT_COUNT) {
        return 0;
    }
    __atomic_store_n(&fault_points[point].remaining,
                     successes_before_failure,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&fault_points[point].armed, 1u, __ATOMIC_RELEASE);
    return 1;
}

int kernel_fault_injection_should_fail(uint32_t point) {
    if (point >= KERNEL_FAULT_POINT_COUNT) {
        return 0;
    }
    KernelFaultPointState* state = &fault_points[point];
    __atomic_add_fetch(&state->attempts, 1u, __ATOMIC_RELAXED);
    if (__atomic_load_n(&state->armed, __ATOMIC_ACQUIRE) == 0) {
        return 0;
    }

    while (__atomic_load_n(&state->armed, __ATOMIC_ACQUIRE) != 0) {
        uint64_t remaining =
            __atomic_load_n(&state->remaining, __ATOMIC_RELAXED);
        if (remaining != 0) {
            if (__atomic_compare_exchange_n(&state->remaining,
                                            &remaining,
                                            remaining - 1u,
                                            0,
                                            __ATOMIC_ACQ_REL,
                                            __ATOMIC_RELAXED)) {
                return 0;
            }
            continue;
        }

        uint32_t armed = 1;
        if (__atomic_compare_exchange_n(&state->armed,
                                        &armed,
                                        0u,
                                        0,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            __atomic_add_fetch(&state->failures, 1u, __ATOMIC_RELAXED);
            return 1;
        }
    }
    return 0;
}

void kernel_fault_injection_get_snapshot(KernelFaultInjectionSnapshot* snapshot) {
    if (snapshot == 0) {
        return;
    }
    for (uint32_t i = 0; i < KERNEL_FAULT_POINT_COUNT; i++) {
        snapshot->points[i].attempts =
            __atomic_load_n(&fault_points[i].attempts, __ATOMIC_RELAXED);
        snapshot->points[i].failures =
            __atomic_load_n(&fault_points[i].failures, __ATOMIC_RELAXED);
        snapshot->points[i].remaining =
            __atomic_load_n(&fault_points[i].remaining, __ATOMIC_RELAXED);
        snapshot->points[i].armed =
            __atomic_load_n(&fault_points[i].armed, __ATOMIC_ACQUIRE);
        snapshot->points[i].reserved = 0;
    }
}

const char* kernel_fault_point_name(uint32_t point) {
    return point < KERNEL_FAULT_POINT_COUNT ? fault_names[point] : "invalid";
}

uint32_t kernel_fault_point_from_name(const char* name) {
    for (uint32_t i = 0; i < KERNEL_FAULT_POINT_COUNT; i++) {
        if (text_equal(name, fault_names[i])) {
            return i;
        }
    }
    return KERNEL_FAULT_POINT_INVALID;
}
