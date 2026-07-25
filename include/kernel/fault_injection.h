#ifndef KERNEL_FAULT_INJECTION_H
#define KERNEL_FAULT_INJECTION_H

#include <stdint.h>

#define KERNEL_FAULT_POINT_PMM 0u
#define KERNEL_FAULT_POINT_HEAP 1u
#define KERNEL_FAULT_POINT_PROCESS 2u
#define KERNEL_FAULT_POINT_MAILBOX 3u
#define KERNEL_FAULT_POINT_SERVICE 4u
#define KERNEL_FAULT_POINT_HANDLE 5u
#define KERNEL_FAULT_POINT_SHARED_MEMORY 6u
#define KERNEL_FAULT_POINT_THREAD_RECORD 7u
#define KERNEL_FAULT_POINT_THREAD_USER_STACK 8u
#define KERNEL_FAULT_POINT_THREAD_KERNEL_STACK 9u
#define KERNEL_FAULT_POINT_THREAD_MAPPING 10u
#define KERNEL_FAULT_POINT_THREAD_WAIT 11u
#define KERNEL_FAULT_POINT_SYNC_OBJECT 12u
#define KERNEL_FAULT_POINT_COUNT 13u
#define KERNEL_FAULT_POINT_INVALID 0xFFFFFFFFu

struct KernelFaultPointStats {
    uint64_t attempts;
    uint64_t failures;
    uint64_t remaining;
    uint32_t armed;
    uint32_t reserved;
};

struct KernelFaultInjectionSnapshot {
    KernelFaultPointStats points[KERNEL_FAULT_POINT_COUNT];
};

void kernel_fault_injection_reset();
int kernel_fault_injection_arm(uint32_t point, uint64_t successes_before_failure);
int kernel_fault_injection_should_fail(uint32_t point);
void kernel_fault_injection_get_snapshot(KernelFaultInjectionSnapshot* snapshot);
const char* kernel_fault_point_name(uint32_t point);
uint32_t kernel_fault_point_from_name(const char* name);

#endif
