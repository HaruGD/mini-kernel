#ifndef KERNEL_SPINLOCK_H
#define KERNEL_SPINLOCK_H

#include <stdint.h>

#define KERNEL_LOCK_CLASS_NONE 0u
#define KERNEL_LOCK_CLASS_PROCESS 1u
#define KERNEL_LOCK_CLASS_ADDRESS_SPACE 2u
#define KERNEL_LOCK_CLASS_HANDLE 3u
#define KERNEL_LOCK_CLASS_IPC_SERVICE 4u
#define KERNEL_LOCK_CLASS_VFS_DEVICE 5u
#define KERNEL_LOCK_STACK_MAX 8u
#define KERNEL_LOCK_NO_OWNER 0xFFFFFFFFu

struct KernelSpinlock {
    volatile uint32_t locked;
    uint32_t lock_class;
    volatile uint32_t owner_cpu;
    const char* name;
};

struct KernelSpinlockToken {
    uint64_t interrupt_flags;
    uint32_t previous_class;
    uint32_t owner_cpu;
    uint8_t acquired;
    uint8_t recursive;
    uint8_t reserved[2];
};

struct KernelSpinlockStats {
    uint64_t acquisitions;
    uint64_t contentions;
    uint64_t order_violations;
    uint64_t recursion_violations;
    uint64_t release_violations;
    uint64_t wrong_cpu_violations;
    uint64_t schedule_violations;
    uint32_t current_depth;
    uint32_t maximum_depth;
    uint32_t current_class;
    uint32_t preemption_disable_depth;
    uint32_t interrupts_enabled;
    uint32_t last_violation_type;
    uint32_t last_held_class;
    uint32_t last_requested_class;
};

#define KERNEL_SPINLOCK_INITIALIZER(lock_class_value, lock_name_value) \
    {0u, (lock_class_value), KERNEL_LOCK_NO_OWNER, (lock_name_value)}

void kernel_spinlock_init(KernelSpinlock* lock, uint32_t lock_class, const char* name);
int kernel_spinlock_acquire(KernelSpinlock* lock, KernelSpinlockToken* token);
void kernel_spinlock_release(KernelSpinlock* lock, KernelSpinlockToken* token);
void kernel_spinlock_get_stats(KernelSpinlockStats* stats);
void kernel_spinlock_reset_stats();
int kernel_interrupts_enabled();
uint32_t kernel_spinlock_depth();
uint32_t kernel_preemption_disable_depth();
int kernel_spinlock_assert_can_schedule();

#ifdef OS64_HOST_TEST
void kernel_host_set_interrupts_enabled(int enabled);
#endif

#endif
