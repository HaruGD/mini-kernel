#include "kernel/spinlock.h"

static volatile uint64_t stat_acquisitions = 0;
static volatile uint64_t stat_contentions = 0;
static volatile uint64_t stat_order_violations = 0;
static volatile uint64_t stat_recursion_violations = 0;
static volatile uint64_t stat_release_violations = 0;
static volatile uint32_t stat_maximum_depth = 0;
static volatile uint32_t stat_last_violation_type = 0;
static volatile uint32_t stat_last_held_class = 0;
static volatile uint32_t stat_last_requested_class = 0;

#ifdef OS64_HOST_TEST
static thread_local uint32_t held_depth = 0;
static thread_local KernelSpinlock* held_locks[KERNEL_LOCK_STACK_MAX];
static thread_local int host_interrupts_enabled = 1;
#else
static uint32_t held_depth = 0;
static KernelSpinlock* held_locks[KERNEL_LOCK_STACK_MAX];
#endif

static void stat_increment(volatile uint64_t* value) {
    __atomic_add_fetch(value, 1u, __ATOMIC_RELAXED);
}

static void record_violation(uint32_t type, uint32_t requested_class) {
    __atomic_store_n(&stat_last_violation_type, type, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_last_held_class,
                     held_depth != 0 ? held_locks[held_depth - 1]->lock_class : 0,
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
    if (lock == 0) {
        return;
    }
    lock->locked = 0;
    lock->lock_class = lock_class;
    lock->name = name;
}

int kernel_spinlock_acquire(KernelSpinlock* lock, KernelSpinlockToken* token) {
    if (token == 0) {
        return 0;
    }
    token->interrupt_flags = interrupt_save_disable();
    token->previous_class = held_depth != 0 ? held_locks[held_depth - 1]->lock_class : 0;
    token->acquired = 0;
    token->recursive = 0;

    if (lock == 0 || lock->lock_class == KERNEL_LOCK_CLASS_NONE ||
        lock->lock_class > KERNEL_LOCK_CLASS_VFS_DEVICE || held_depth >= KERNEL_LOCK_STACK_MAX) {
        stat_increment(&stat_order_violations);
        record_violation(1, lock != 0 ? lock->lock_class : 0);
        interrupt_restore(token->interrupt_flags);
        return 0;
    }
    if (held_depth != 0 && held_locks[held_depth - 1] == lock) {
        stat_increment(&stat_recursion_violations);
        record_violation(2, lock->lock_class);
        token->acquired = 1;
        token->recursive = 1;
        return 1;
    }
    if (held_depth != 0 && lock->lock_class <= held_locks[held_depth - 1]->lock_class) {
        stat_increment(&stat_order_violations);
        record_violation(1, lock->lock_class);
        interrupt_restore(token->interrupt_flags);
        return 0;
    }

    while (__atomic_exchange_n(&lock->locked, 1u, __ATOMIC_ACQUIRE) != 0) {
        stat_increment(&stat_contentions);
#if defined(__x86_64__) || defined(_M_X64)
        __asm__ volatile("pause");
#endif
    }
    held_locks[held_depth++] = lock;
    uint32_t maximum = __atomic_load_n(&stat_maximum_depth, __ATOMIC_RELAXED);
    while (held_depth > maximum &&
           !__atomic_compare_exchange_n(&stat_maximum_depth,
                                        &maximum,
                                        held_depth,
                                        0,
                                        __ATOMIC_RELAXED,
                                        __ATOMIC_RELAXED)) {
    }
    stat_increment(&stat_acquisitions);
    token->acquired = 1;
    return 1;
}

void kernel_spinlock_release(KernelSpinlock* lock, KernelSpinlockToken* token) {
    if (token == 0 || !token->acquired || lock == 0) {
        stat_increment(&stat_release_violations);
        record_violation(3, lock != 0 ? lock->lock_class : 0);
        return;
    }
    if (token->recursive) {
        token->acquired = 0;
        interrupt_restore(token->interrupt_flags);
        return;
    }
    if (held_depth == 0 || held_locks[held_depth - 1] != lock) {
        stat_increment(&stat_release_violations);
        record_violation(3, lock->lock_class);
        token->acquired = 0;
        interrupt_restore(token->interrupt_flags);
        return;
    }
    held_locks[--held_depth] = 0;
    __atomic_store_n(&lock->locked, 0u, __ATOMIC_RELEASE);
    token->acquired = 0;
    interrupt_restore(token->interrupt_flags);
}

void kernel_spinlock_get_stats(KernelSpinlockStats* stats) {
    if (stats == 0) {
        return;
    }
    stats->acquisitions = __atomic_load_n(&stat_acquisitions, __ATOMIC_RELAXED);
    stats->contentions = __atomic_load_n(&stat_contentions, __ATOMIC_RELAXED);
    stats->order_violations = __atomic_load_n(&stat_order_violations, __ATOMIC_RELAXED);
    stats->recursion_violations = __atomic_load_n(&stat_recursion_violations, __ATOMIC_RELAXED);
    stats->release_violations = __atomic_load_n(&stat_release_violations, __ATOMIC_RELAXED);
    stats->current_depth = held_depth;
    stats->maximum_depth = __atomic_load_n(&stat_maximum_depth, __ATOMIC_RELAXED);
    stats->current_class = held_depth != 0 ? held_locks[held_depth - 1]->lock_class : 0;
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
    __atomic_store_n(&stat_maximum_depth, held_depth, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_last_violation_type, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_last_held_class, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&stat_last_requested_class, 0u, __ATOMIC_RELAXED);
}

#ifdef OS64_HOST_TEST
void kernel_host_set_interrupts_enabled(int enabled) {
    host_interrupts_enabled = enabled ? 1 : 0;
}
#endif
