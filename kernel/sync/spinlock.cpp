#include "kernel/spinlock.h"

#ifndef OS64_HOST_TEST
#include "kernel/cpu_local.h"
#endif

static volatile uint64_t stat_acquisitions = 0;
static volatile uint64_t stat_contentions = 0;
static volatile uint64_t stat_order_violations = 0;
static volatile uint64_t stat_recursion_violations = 0;
static volatile uint64_t stat_release_violations = 0;
static volatile uint64_t stat_wrong_cpu_violations = 0;
static volatile uint64_t stat_schedule_violations = 0;
static volatile uint64_t stat_tlb_wait_violations = 0;
static volatile uint64_t stat_tlb_wait_entries = 0;
static volatile uint32_t stat_maximum_depth = 0;
static volatile uint32_t stat_last_violation_type = 0;
static volatile uint32_t stat_last_held_class = 0;
static volatile uint32_t stat_last_requested_class = 0;

#ifdef OS64_HOST_TEST
static thread_local uint32_t held_depth = 0;
static thread_local uint32_t preemption_depth = 0;
static thread_local uint32_t tlb_wait_depth = 0;
static thread_local KernelSpinlock* held_locks[KERNEL_LOCK_STACK_MAX];
static thread_local int host_interrupts_enabled = 1;
static thread_local uint32_t host_cpu_id = 0xFFFFFFFFu;
static volatile uint32_t next_host_cpu_id = 0;

static uint32_t current_cpu_id() {
    if (host_cpu_id == 0xFFFFFFFFu) {
        host_cpu_id = __atomic_fetch_add(&next_host_cpu_id, 1u, __ATOMIC_RELAXED);
    }
    return host_cpu_id;
}

static KernelSpinlock** current_locks() { return held_locks; }
static uint32_t* current_depth() { return &held_depth; }
static uint32_t* current_preemption_depth() { return &preemption_depth; }
static uint32_t* current_tlb_wait_depth() { return &tlb_wait_depth; }
#else
static CpuLocal* checked_local() {
    CpuLocal* local = cpu_local_current();
    return cpu_local_validate(local) ? local : 0;
}

static uint32_t current_cpu_id() {
    CpuLocal* local = checked_local();
    return local != 0 ? local->logical_id : 0xFFFFFFFFu;
}

static KernelSpinlock** current_locks() {
    CpuLocal* local = checked_local();
    return local != 0 ? local->held_locks : 0;
}

static uint32_t* current_depth() {
    CpuLocal* local = checked_local();
    return local != 0 ? &local->held_lock_depth : 0;
}

static uint32_t* current_preemption_depth() {
    CpuLocal* local = checked_local();
    return local != 0 ? &local->preemption_disable_count : 0;
}

static uint32_t* current_tlb_wait_depth() {
    CpuLocal* local = checked_local();
    return local != 0 ? &local->tlb_wait_depth : 0;
}
#endif

static void stat_increment(volatile uint64_t* value) {
    __atomic_add_fetch(value, 1u, __ATOMIC_RELAXED);
}

static void record_violation(uint32_t type, uint32_t requested_class) {
    uint32_t* depth = current_depth();
    KernelSpinlock** locks = current_locks();
    __atomic_store_n(&stat_last_violation_type, type, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_last_held_class,
                     depth != 0 && locks != 0 && *depth != 0
                         ? locks[*depth - 1]->lock_class : 0,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&stat_last_requested_class, requested_class, __ATOMIC_RELAXED);
}

static uint64_t interrupt_save_disable() {
#ifdef OS64_HOST_TEST
    uint64_t flags = host_interrupts_enabled ? (1ULL << 9) : 0;
    host_interrupts_enabled = 0;
    return flags;
#else
    uint64_t flags = 0;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
#endif
}

static void interrupt_restore(uint64_t flags) {
#ifdef OS64_HOST_TEST
    host_interrupts_enabled = (flags & (1ULL << 9)) != 0 ? 1 : 0;
#else
    if ((flags & (1ULL << 9)) != 0) {
        __asm__ volatile("sti" : : : "memory");
    }
#endif
}

int kernel_interrupts_enabled() {
#ifdef OS64_HOST_TEST
    return host_interrupts_enabled;
#else
    uint64_t flags = 0;
    __asm__ volatile("pushfq; pop %0" : "=r"(flags));
    return (flags & (1ULL << 9)) != 0;
#endif
}

void kernel_spinlock_init(KernelSpinlock* lock, uint32_t lock_class, const char* name) {
    if (lock == 0) return;
    lock->locked = 0;
    lock->lock_class = lock_class;
    lock->owner_cpu = KERNEL_LOCK_NO_OWNER;
    lock->name = name;
}

int kernel_spinlock_acquire(KernelSpinlock* lock, KernelSpinlockToken* token) {
    if (token == 0) return 0;
    uint32_t* depth = current_depth();
    uint32_t* preempt = current_preemption_depth();
    KernelSpinlock** locks = current_locks();
    token->interrupt_flags = interrupt_save_disable();
    token->previous_class =
        depth != 0 && locks != 0 && *depth != 0
            ? locks[*depth - 1]->lock_class : 0;
    token->owner_cpu = current_cpu_id();
    token->acquired = 0;
    token->recursive = 0;

    if (depth == 0 || preempt == 0 || locks == 0 ||
        token->owner_cpu == KERNEL_LOCK_NO_OWNER ||
        kernel_in_tlb_wait()) {
        stat_increment(&stat_order_violations);
        record_violation(1, lock != 0 ? lock->lock_class : 0);
        interrupt_restore(token->interrupt_flags);
        return 0;
    }
    (*preempt)++;
    if (lock == 0 || lock->lock_class == KERNEL_LOCK_CLASS_NONE ||
        lock->lock_class > KERNEL_LOCK_CLASS_VFS_DEVICE ||
        *depth >= KERNEL_LOCK_STACK_MAX) {
        stat_increment(&stat_order_violations);
        record_violation(1, lock != 0 ? lock->lock_class : 0);
        (*preempt)--;
        interrupt_restore(token->interrupt_flags);
        return 0;
    }
    if (*depth != 0 && locks[*depth - 1] == lock) {
        stat_increment(&stat_recursion_violations);
        record_violation(2, lock->lock_class);
        (*preempt)--;
        interrupt_restore(token->interrupt_flags);
        return 0;
    }
    if (*depth != 0 && lock->lock_class <= locks[*depth - 1]->lock_class) {
        stat_increment(&stat_order_violations);
        record_violation(1, lock->lock_class);
        (*preempt)--;
        interrupt_restore(token->interrupt_flags);
        return 0;
    }

    while (__atomic_exchange_n(&lock->locked, 1u, __ATOMIC_ACQUIRE) != 0) {
        stat_increment(&stat_contentions);
#if defined(__x86_64__) || defined(_M_X64)
        __asm__ volatile("pause");
#endif
    }
    lock->owner_cpu = token->owner_cpu;
    locks[(*depth)++] = lock;
    uint32_t maximum = __atomic_load_n(&stat_maximum_depth, __ATOMIC_RELAXED);
    while (*depth > maximum &&
           !__atomic_compare_exchange_n(&stat_maximum_depth,
                                        &maximum,
                                        *depth,
                                        0,
                                        __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED)) {
    }
    stat_increment(&stat_acquisitions);
    token->acquired = 1;
    return 1;
}

void kernel_spinlock_release(KernelSpinlock* lock, KernelSpinlockToken* token) {
    uint32_t* depth = current_depth();
    uint32_t* preempt = current_preemption_depth();
    KernelSpinlock** locks = current_locks();
    if (token == 0 || !token->acquired || lock == 0 ||
        depth == 0 || preempt == 0 || locks == 0) {
        stat_increment(&stat_release_violations);
        record_violation(3, lock != 0 ? lock->lock_class : 0);
        return;
    }
    const uint32_t cpu = current_cpu_id();
    if (token->owner_cpu != cpu || lock->owner_cpu != cpu) {
        stat_increment(&stat_release_violations);
        stat_increment(&stat_wrong_cpu_violations);
        record_violation(4, lock->lock_class);
        return;
    }
    if (*depth == 0 || locks[*depth - 1] != lock || *preempt == 0) {
        stat_increment(&stat_release_violations);
        record_violation(3, lock->lock_class);
        return;
    }
    locks[--(*depth)] = 0;
    lock->owner_cpu = KERNEL_LOCK_NO_OWNER;
    __atomic_store_n(&lock->locked, 0u, __ATOMIC_RELEASE);
    (*preempt)--;
    token->acquired = 0;
    interrupt_restore(token->interrupt_flags);
}

uint32_t kernel_spinlock_depth() {
    uint32_t* depth = current_depth();
    return depth != 0 ? *depth : 0;
}

uint32_t kernel_preemption_disable_depth() {
    uint32_t* depth = current_preemption_depth();
    return depth != 0 ? *depth : 0;
}

int kernel_spinlock_assert_can_schedule() {
    if (kernel_spinlock_depth() != 0 ||
        kernel_preemption_disable_depth() != 0) {
        stat_increment(&stat_schedule_violations);
        record_violation(5, 0);
        return 0;
    }
    return 1;
}

int kernel_tlb_wait_enter() {
    uint32_t* preempt = current_preemption_depth();
    uint32_t* wait_depth = current_tlb_wait_depth();
    if (preempt == 0 || wait_depth == 0 ||
        kernel_spinlock_depth() != 0 || !kernel_interrupts_enabled() ||
        *wait_depth != 0) {
        stat_increment(&stat_tlb_wait_violations);
        record_violation(6, 0);
        return 0;
    }
    (*preempt)++;
    *wait_depth = 1;
    stat_increment(&stat_tlb_wait_entries);
    return 1;
}

void kernel_tlb_wait_leave() {
    uint32_t* preempt = current_preemption_depth();
    uint32_t* wait_depth = current_tlb_wait_depth();
    if (preempt == 0 || wait_depth == 0 || *wait_depth != 1 ||
        *preempt == 0) {
        stat_increment(&stat_tlb_wait_violations);
        record_violation(6, 0);
        return;
    }
    *wait_depth = 0;
    (*preempt)--;
}

int kernel_in_tlb_wait() {
    uint32_t* wait_depth = current_tlb_wait_depth();
    return wait_depth != 0 && *wait_depth != 0;
}

void kernel_spinlock_get_stats(KernelSpinlockStats* stats) {
    if (stats == 0) return;
    uint32_t* depth = current_depth();
    KernelSpinlock** locks = current_locks();
    stats->acquisitions = __atomic_load_n(&stat_acquisitions, __ATOMIC_RELAXED);
    stats->contentions = __atomic_load_n(&stat_contentions, __ATOMIC_RELAXED);
    stats->order_violations = __atomic_load_n(&stat_order_violations, __ATOMIC_RELAXED);
    stats->recursion_violations = __atomic_load_n(&stat_recursion_violations, __ATOMIC_RELAXED);
    stats->release_violations = __atomic_load_n(&stat_release_violations, __ATOMIC_RELAXED);
    stats->wrong_cpu_violations = __atomic_load_n(&stat_wrong_cpu_violations, __ATOMIC_RELAXED);
    stats->schedule_violations = __atomic_load_n(&stat_schedule_violations, __ATOMIC_RELAXED);
    stats->tlb_wait_violations = __atomic_load_n(&stat_tlb_wait_violations, __ATOMIC_RELAXED);
    stats->tlb_wait_entries = __atomic_load_n(&stat_tlb_wait_entries, __ATOMIC_RELAXED);
    stats->current_depth = depth != 0 ? *depth : 0;
    stats->maximum_depth = __atomic_load_n(&stat_maximum_depth, __ATOMIC_RELAXED);
    stats->current_class =
        depth != 0 && locks != 0 && *depth != 0
            ? locks[*depth - 1]->lock_class : 0;
    stats->preemption_disable_depth = kernel_preemption_disable_depth();
    stats->interrupts_enabled = kernel_interrupts_enabled() ? 1u : 0u;
    stats->last_violation_type = __atomic_load_n(&stat_last_violation_type, __ATOMIC_RELAXED);
    stats->last_held_class = __atomic_load_n(&stat_last_held_class, __ATOMIC_RELAXED);
    stats->last_requested_class = __atomic_load_n(&stat_last_requested_class, __ATOMIC_RELAXED);
}

void kernel_spinlock_reset_stats() {
    __atomic_store_n(&stat_acquisitions, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_contentions, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_order_violations, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_recursion_violations, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_release_violations, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_wrong_cpu_violations, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_schedule_violations, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_tlb_wait_violations, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_tlb_wait_entries, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_maximum_depth, kernel_spinlock_depth(), __ATOMIC_RELAXED);
    __atomic_store_n(&stat_last_violation_type, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_last_held_class, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_last_requested_class, 0u, __ATOMIC_RELAXED);
}

#ifdef OS64_HOST_TEST
void kernel_host_set_interrupts_enabled(int enabled) {
    host_interrupts_enabled = enabled ? 1 : 0;
}
#endif
