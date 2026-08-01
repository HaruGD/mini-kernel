#include "kernel/driver/driver_alloc.h"

#include "kernel/cpu.h"
#include "kernel/cpu_local.h"
#include "kernel/fault_injection.h"
#include "kernel/kutil64.h"
#include "kernel/mm/heap.h"
#include "kernel/mm/vm.h"
#include "kernel/spinlock.h"

#ifdef OS64_HOST_TEST
#include <stdlib.h>
#endif

#define DRIVER_ALLOC_PAGE_BASE  0x0000000060000000ULL
#define DRIVER_ALLOC_PAGE_LIMIT 0x0000000064000000ULL
#define DRIVER_ALLOC_PAGE_COUNT ((DRIVER_ALLOC_PAGE_LIMIT - DRIVER_ALLOC_PAGE_BASE) / VM_PAGE_SIZE)
#define DRIVER_ALLOC_BITMAP_WORDS (DRIVER_ALLOC_PAGE_COUNT / 64u)

#define ALLOCATION_FREE 0u
#define ALLOCATION_PENDING 1u
#define ALLOCATION_ACTIVE 2u
#define ALLOCATION_RELEASING 3u
#define ALLOCATION_QUARANTINED 4u

struct DriverAllocationRecord {
    uint8_t state;
    uint8_t context;
    uint16_t flags;
    uint32_t slot;
    uint32_t generation;
    DriverIdentity owner;
    DriverResourceHandle resource;
    void* address;
    void* backing;
    uint64_t requested_size;
    uint64_t charged_size;
    uint64_t alignment;
    uint32_t page_index;
    uint32_t page_count;
    uint32_t atomic_slot;
    uint32_t pin_count;
    char tag[24];
};

static DriverAllocationRecord g_allocations[DRIVER_MAX_ALLOCATIONS];
static uint64_t g_page_bitmap[DRIVER_ALLOC_BITMAP_WORDS];
alignas(16) static uint8_t
    g_atomic_storage[DRIVER_MAX_RECORDS][DRIVER_ATOMIC_SLOT_COUNT]
                    [DRIVER_ATOMIC_SLOT_SIZE];
static uint8_t g_atomic_in_use[DRIVER_MAX_RECORDS][DRIVER_ATOMIC_SLOT_COUNT];
static DriverExecutionContext g_cpu_contexts[CPU_MAX_COUNT];
static DriverAllocationStats g_stats;
static uint8_t g_initialized;
static KernelSpinlock g_allocation_lock =
    KERNEL_SPINLOCK_INITIALIZER(KERNEL_LOCK_CLASS_VFS_DEVICE,
                                "driver_allocation");

#ifdef OS64_HOST_TEST
static thread_local DriverExecutionContext g_host_context;
#endif

static uint32_t next_generation(uint32_t generation) {
    generation++;
    return generation != 0 ? generation : 1u;
}

static void bytes_zero(void* pointer, uint64_t size) {
    uint8_t* bytes = (uint8_t*)pointer;
    for (uint64_t i = 0; i < size; i++) bytes[i] = 0;
}

static int is_power_of_two(uint64_t value) {
    return value != 0 && (value & (value - 1ULL)) == 0;
}

static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1ULL) & ~(alignment - 1ULL);
}

static DriverExecutionContext* current_context_slot(uint32_t* out_slot) {
#ifdef OS64_HOST_TEST
    if (out_slot != 0) *out_slot = 0;
    return &g_host_context;
#else
    CpuLocal* local = cpu_local_current();
    if (!cpu_local_validate(local) || local->logical_id >= CPU_MAX_COUNT) {
        return 0;
    }
    if (out_slot != 0) *out_slot = local->logical_id;
    return &g_cpu_contexts[local->logical_id];
#endif
}

void driver_allocation_init() {
    kernel_spinlock_init(&g_allocation_lock, KERNEL_LOCK_CLASS_VFS_DEVICE,
                         "driver_allocation");
    for (uint32_t i = 0; i < DRIVER_MAX_ALLOCATIONS; i++) {
        const uint32_t generation = g_allocations[i].generation;
        g_allocations[i] = {};
        g_allocations[i].slot = i;
        g_allocations[i].generation = generation;
        g_allocations[i].resource = driver_resource_invalid();
        g_allocations[i].atomic_slot = DRIVER_ATOMIC_SLOT_COUNT;
    }
    for (uint32_t i = 0; i < DRIVER_ALLOC_BITMAP_WORDS; i++) {
        g_page_bitmap[i] = 0;
    }
    for (uint32_t owner = 0; owner < DRIVER_MAX_RECORDS; owner++) {
        for (uint32_t slot = 0; slot < DRIVER_ATOMIC_SLOT_COUNT; slot++) {
            g_atomic_in_use[owner][slot] = 0;
            bytes_zero(g_atomic_storage[owner][slot], DRIVER_ATOMIC_SLOT_SIZE);
        }
    }
    for (uint32_t cpu = 0; cpu < CPU_MAX_COUNT; cpu++) {
        g_cpu_contexts[cpu] = {};
        g_cpu_contexts[cpu].owner = driver_identity_invalid();
    }
#ifdef OS64_HOST_TEST
    g_host_context = {};
    g_host_context.owner = driver_identity_invalid();
#endif
    g_stats = {};
    g_initialized = 1;
}

DriverAllocationHandle driver_allocation_invalid() {
    DriverAllocationHandle handle = {DRIVER_IDENTITY_INVALID_SLOT, 0};
    return handle;
}

int driver_allocation_handle_is_valid(DriverAllocationHandle handle) {
    return handle.slot < DRIVER_MAX_ALLOCATIONS && handle.generation != 0;
}

static int driver_execution_enter_common(DriverIdentity owner, uint32_t kind,
                                         DriverExecutionToken* token,
                                         int quiesce_entry) {
    if (token == 0 || !driver_manager_identity_is_live(owner) ||
        kind < DRIVER_CONTEXT_THREAD_SLEEPABLE ||
        kind > DRIVER_CONTEXT_EMERGENCY) {
        return DRIVER_LOAD_CONTEXT_DENIED;
    }
    uint32_t cpu_slot = 0;
    DriverExecutionContext* current = current_context_slot(&cpu_slot);
    if (current == 0) return DRIVER_LOAD_CONTEXT_DENIED;
    *token = {};
    token->activity.owner = driver_identity_invalid();
    if (!quiesce_entry) {
        const uint32_t activity = kind == DRIVER_CONTEXT_IRQ
            ? DRIVER_ACTIVITY_IRQ : DRIVER_ACTIVITY_CALL;
        int pin_result = driver_manager_activity_pin(owner, activity,
                                                     &token->activity);
        if (pin_result != DRIVER_LOAD_OK) return pin_result;
    }
    token->previous = *current;
    token->cpu_slot = cpu_slot;
    token->active = 1;
    current->owner = owner;
    current->kind = kind;
    current->depth = token->previous.depth + 1u;
    return DRIVER_LOAD_OK;
}

int driver_execution_enter(DriverIdentity owner, uint32_t kind,
                           DriverExecutionToken* token) {
    return driver_execution_enter_common(owner, kind, token, 0);
}

int driver_execution_enter_quiesce(DriverIdentity owner, uint32_t kind,
                                   DriverExecutionToken* token) {
    if (!driver_manager_identity_is_live(owner)) {
        return DRIVER_LOAD_CONTEXT_DENIED;
    }
    return driver_execution_enter_common(owner, kind, token, 1);
}

void driver_execution_leave(DriverExecutionToken* token) {
    if (token == 0 || !token->active) return;
    uint32_t cpu_slot = 0;
    DriverExecutionContext* current = current_context_slot(&cpu_slot);
    if (current != 0 && cpu_slot == token->cpu_slot) {
        *current = token->previous;
    }
    driver_manager_activity_unpin(&token->activity);
    token->active = 0;
}

int driver_execution_current(DriverExecutionContext* out) {
    if (out == 0) return 0;
    DriverExecutionContext* current = current_context_slot(0);
    if (current == 0 || current->depth == 0 ||
        !driver_manager_identity_is_live(current->owner)) {
        *out = {};
        out->owner = driver_identity_invalid();
        return 0;
    }
#ifndef OS64_HOST_TEST
    CpuLocal* local = cpu_local_current();
    if (cpu_local_validate(local) && local->emergency_active) {
        *out = *current;
        out->kind = DRIVER_CONTEXT_EMERGENCY;
        return 1;
    }
#endif
    *out = *current;
    return 1;
}

int driver_execution_require_sleepable() {
    DriverExecutionContext context;
    return driver_execution_current(&context) &&
           context.kind == DRIVER_CONTEXT_THREAD_SLEEPABLE;
}

int driver_execution_runtime_allowed() {
    DriverExecutionContext context;
    return driver_execution_current(&context) &&
           context.kind != DRIVER_CONTEXT_EMERGENCY;
}

static uint64_t owner_bytes_locked(DriverIdentity owner) {
    uint64_t bytes = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_ALLOCATIONS; i++) {
        const DriverAllocationRecord* record = &g_allocations[i];
        if (record->state != ALLOCATION_FREE &&
            driver_identity_equal(record->owner, owner)) {
            bytes += record->charged_size;
        }
    }
    return bytes;
}

static int bitmap_range_free(uint32_t begin, uint32_t count) {
    for (uint32_t page = 0; page < count; page++) {
        const uint32_t index = begin + page;
        if ((g_page_bitmap[index / 64u] & (1ULL << (index % 64u))) != 0) {
            return 0;
        }
    }
    return 1;
}

static void bitmap_set(uint32_t begin, uint32_t count, int used) {
    for (uint32_t page = 0; page < count; page++) {
        const uint32_t index = begin + page;
        const uint64_t bit = 1ULL << (index % 64u);
        if (used) g_page_bitmap[index / 64u] |= bit;
        else g_page_bitmap[index / 64u] &= ~bit;
    }
}

static int reserve_page_run(uint32_t count, uint32_t alignment_pages,
                            uint32_t* out_index) {
    if (count == 0 || alignment_pages == 0 || out_index == 0) return 0;
    for (uint32_t begin = 0; begin <= DRIVER_ALLOC_PAGE_COUNT - count;) {
        const uint32_t aligned = (begin + alignment_pages - 1u) &
                                 ~(alignment_pages - 1u);
        if (aligned > DRIVER_ALLOC_PAGE_COUNT - count) break;
        if (bitmap_range_free(aligned, count)) {
            bitmap_set(aligned, count, 1);
            *out_index = aligned;
            return 1;
        }
        begin = aligned + 1u;
    }
    return 0;
}

static DriverAllocationRecord* reserve_record_locked(
    DriverIdentity owner, uint32_t context, uint64_t size,
    uint64_t alignment, uint32_t flags, const char* tag,
    uint64_t charged_size, int* out_error) {
    if (out_error != 0) *out_error = DRIVER_LOAD_NO_SLOT;
    if (owner_bytes_locked(owner) > DRIVER_ALLOCATION_BUDGET_BYTES - charged_size) {
        g_stats.budget_rejections++;
        if (out_error != 0) *out_error = DRIVER_LOAD_ALLOCATION_BUDGET;
        return 0;
    }
    DriverAllocationRecord* record = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_ALLOCATIONS; i++) {
        if (g_allocations[i].state == ALLOCATION_FREE) {
            record = &g_allocations[i];
            break;
        }
    }
    if (record == 0) {
        g_stats.exhaustion_failures++;
        return 0;
    }
    record->generation = next_generation(record->generation);
    record->state = ALLOCATION_PENDING;
    record->context = (uint8_t)context;
    record->flags = (uint16_t)flags;
    record->owner = owner;
    record->resource = driver_resource_invalid();
    record->address = 0;
    record->backing = 0;
    record->requested_size = size;
    record->charged_size = charged_size;
    record->alignment = alignment;
    record->page_index = 0;
    record->page_count = 0;
    record->atomic_slot = DRIVER_ATOMIC_SLOT_COUNT;
    copy_string64(record->tag, sizeof(record->tag), tag != 0 ? tag : "");
    return record;
}

static void clear_record_locked(DriverAllocationRecord* record) {
    const uint32_t slot = record->slot;
    const uint32_t generation = record->generation;
    *record = {};
    record->slot = slot;
    record->generation = generation;
    record->owner = driver_identity_invalid();
    record->resource = driver_resource_invalid();
    record->atomic_slot = DRIVER_ATOMIC_SLOT_COUNT;
}

static int context_allows(uint32_t context, uint32_t flags) {
    if (context == DRIVER_CONTEXT_THREAD_SLEEPABLE) return 1;
    if (context == DRIVER_CONTEXT_THREAD_ATOMIC || context == DRIVER_CONTEXT_IRQ) {
        return (flags & DRIVER_ALLOC_ATOMIC) != 0 &&
               (flags & DRIVER_ALLOC_PAGES) == 0;
    }
    return 0;
}

int driver_allocation_create(DriverIdentity owner, uint32_t context,
                             uint64_t size, uint64_t alignment,
                             uint32_t flags, const char* tag,
                             DriverAllocationResult* out) {
    if (out != 0) {
        *out = {};
        out->handle = driver_allocation_invalid();
    }
    if (!g_initialized || out == 0 || size == 0 ||
        !is_power_of_two(alignment) || alignment > 0x200000ULL ||
        (flags & ~(DRIVER_ALLOC_ZERO | DRIVER_ALLOC_PAGES |
                   DRIVER_ALLOC_ATOMIC)) != 0 ||
        ((flags & DRIVER_ALLOC_ATOMIC) != 0 &&
         (size > DRIVER_ATOMIC_SLOT_SIZE || alignment > 16))) {
        return DRIVER_LOAD_ALLOCATION_DENIED;
    }
    if (!driver_manager_identity_is_live(owner)) {
        return DRIVER_LOAD_STALE_IDENTITY;
    }
    if (!driver_manager_identity_accepts_resources(owner)) {
        return DRIVER_LOAD_STATE_DENIED;
    }
    if (!context_allows(context, flags)) {
        __atomic_add_fetch(&g_stats.context_rejections, 1u, __ATOMIC_RELAXED);
        return DRIVER_LOAD_CONTEXT_DENIED;
    }
    uint64_t charged_size = size;
    uint32_t page_count = 0;
    if ((flags & DRIVER_ALLOC_PAGES) != 0) {
        charged_size = align_up(size, VM_PAGE_SIZE);
        if (charged_size < size || charged_size > 0xFFFFFFFFULL) {
            return DRIVER_LOAD_ALLOCATION_DENIED;
        }
        page_count = (uint32_t)(charged_size / VM_PAGE_SIZE);
    }
    if (charged_size > DRIVER_ALLOCATION_BUDGET_BYTES) {
        __atomic_add_fetch(&g_stats.budget_rejections, 1u, __ATOMIC_RELAXED);
        return DRIVER_LOAD_ALLOCATION_BUDGET;
    }

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_allocation_lock, &token)) {
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    int reserve_error = DRIVER_LOAD_NO_SLOT;
    DriverAllocationRecord* record =
        kernel_fault_injection_should_fail(
            KERNEL_FAULT_POINT_DRIVER_ALLOC_RECORD)
        ? 0
        : reserve_record_locked(owner, context, size, alignment, flags, tag,
                                charged_size, &reserve_error);
    if (record == 0) {
        kernel_spinlock_release(&g_allocation_lock, &token);
        return reserve_error;
    }
    const uint32_t record_slot = record->slot;
    const uint32_t record_generation = record->generation;
    void* address = 0;
    void* backing = 0;
    uint32_t atomic_slot = DRIVER_ATOMIC_SLOT_COUNT;
    uint32_t page_index = 0;
    const int inject_backing_failure = kernel_fault_injection_should_fail(
        KERNEL_FAULT_POINT_DRIVER_ALLOC) ||
        (((flags & DRIVER_ALLOC_PAGES) != 0) &&
         kernel_fault_injection_should_fail(
             KERNEL_FAULT_POINT_DRIVER_PAGE_RUN));

    if ((flags & DRIVER_ALLOC_ATOMIC) != 0) {
        for (uint32_t i = 0;
             !inject_backing_failure && i < DRIVER_ATOMIC_SLOT_COUNT; i++) {
            if (!g_atomic_in_use[owner.slot][i]) {
                g_atomic_in_use[owner.slot][i] = 1;
                atomic_slot = i;
                address = g_atomic_storage[owner.slot][i];
                backing = address;
                break;
            }
        }
        if (address == 0) g_stats.atomic_exhaustion++;
    } else if ((flags & DRIVER_ALLOC_PAGES) != 0) {
        uint32_t alignment_pages = alignment <= VM_PAGE_SIZE
            ? 1u : (uint32_t)(alignment / VM_PAGE_SIZE);
        if (alignment > VM_PAGE_SIZE &&
            (alignment % VM_PAGE_SIZE) != 0) {
            alignment_pages = 0;
        }
        if (alignment_pages != 0 &&
            reserve_page_run(page_count, alignment_pages, &page_index)) {
#ifdef OS64_HOST_TEST
            const uint64_t host_alignment = alignment > VM_PAGE_SIZE
                ? alignment : VM_PAGE_SIZE;
            if (!inject_backing_failure &&
                posix_memalign(&backing, host_alignment, charged_size) == 0) {
                address = backing;
            }
#else
            const uint64_t virtual_address = DRIVER_ALLOC_PAGE_BASE +
                (uint64_t)page_index * VM_PAGE_SIZE;
            uint32_t mapped = 0;
            if (!inject_backing_failure &&
                vm_alloc_map_range(virtual_address, charged_size,
                                   VM_FLAG_WRITABLE | VM_FLAG_NO_EXECUTE,
                                   &mapped) && mapped == page_count) {
                address = (void*)(uintptr_t)virtual_address;
                backing = address;
            }
#endif
            if (address == 0) bitmap_set(page_index, page_count, 0);
        }
    }
    record->page_index = page_index;
    record->page_count = page_count;
    record->atomic_slot = atomic_slot;
    kernel_spinlock_release(&g_allocation_lock, &token);

    if (!inject_backing_failure &&
        (flags & (DRIVER_ALLOC_ATOMIC | DRIVER_ALLOC_PAGES)) == 0) {
        if (size <= 0xFFFFFFFFULL - alignment) {
            backing = kmalloc((size_t)(size + alignment - 1ULL));
            if (backing != 0) {
                address = (void*)(uintptr_t)align_up(
                    (uint64_t)(uintptr_t)backing, alignment);
            }
        }
    }
    if (address == 0) {
        if (kernel_spinlock_acquire(&g_allocation_lock, &token)) {
            DriverAllocationRecord* pending = &g_allocations[record_slot];
            if (pending->state == ALLOCATION_PENDING &&
                pending->generation == record_generation) {
                clear_record_locked(pending);
                g_stats.backing_failures++;
            }
            kernel_spinlock_release(&g_allocation_lock, &token);
        }
        return DRIVER_LOAD_OUT_OF_MEMORY;
    }
    if ((flags & DRIVER_ALLOC_ZERO) != 0 ||
        (flags & (DRIVER_ALLOC_ATOMIC | DRIVER_ALLOC_PAGES)) != 0) {
        bytes_zero(address, charged_size);
    }

    DriverResourceHandle resource;
    int resource_result = driver_resource_register(
        owner, driver_device_identity_invalid(), DRIVER_RESOURCE_ALLOCATION,
        flags, (uint64_t)(uintptr_t)address, charged_size,
        tag != 0 ? tag : "allocation", &resource);
    if (resource_result != DRIVER_LOAD_OK) {
        if ((flags & DRIVER_ALLOC_ATOMIC) != 0) {
            bytes_zero(address, DRIVER_ATOMIC_SLOT_SIZE);
        } else if ((flags & DRIVER_ALLOC_PAGES) != 0) {
#ifdef OS64_HOST_TEST
            free(backing);
#else
            vm_unmap_free_range((uint64_t)(uintptr_t)address, page_count);
#endif
        } else {
            bytes_zero(address, size);
            kfree(backing);
        }
        if (kernel_spinlock_acquire(&g_allocation_lock, &token)) {
            DriverAllocationRecord* pending = &g_allocations[record_slot];
            if ((flags & DRIVER_ALLOC_ATOMIC) != 0 &&
                atomic_slot < DRIVER_ATOMIC_SLOT_COUNT) {
                g_atomic_in_use[owner.slot][atomic_slot] = 0;
            }
            if ((flags & DRIVER_ALLOC_PAGES) != 0) {
                bitmap_set(page_index, page_count, 0);
            }
            clear_record_locked(pending);
            kernel_spinlock_release(&g_allocation_lock, &token);
        }
        return resource_result;
    }

    if (!kernel_spinlock_acquire(&g_allocation_lock, &token)) {
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    record = &g_allocations[record_slot];
    if (record->state != ALLOCATION_PENDING ||
        record->generation != record_generation) {
        kernel_spinlock_release(&g_allocation_lock, &token);
        return DRIVER_LOAD_STALE_IDENTITY;
    }
    record->resource = resource;
    record->address = address;
    record->backing = backing;
    record->state = ALLOCATION_ACTIVE;
    g_stats.active++;
    g_stats.active_bytes += charged_size;
    g_stats.allocations++;
    if ((flags & DRIVER_ALLOC_ATOMIC) != 0) g_stats.atomic_active++;
    if (g_stats.active > g_stats.high_water) g_stats.high_water = g_stats.active;
    if (g_stats.active_bytes > g_stats.peak_bytes) {
        g_stats.peak_bytes = g_stats.active_bytes;
    }
    out->handle.slot = record_slot;
    out->handle.generation = record_generation;
    out->address = address;
    out->size = size;
    kernel_spinlock_release(&g_allocation_lock, &token);
    return DRIVER_LOAD_OK;
}

static DriverAllocationRecord* resolve_locked(DriverIdentity owner,
                                              DriverAllocationHandle handle) {
    if (!driver_allocation_handle_is_valid(handle)) return 0;
    DriverAllocationRecord* record = &g_allocations[handle.slot];
    if (record->state != ALLOCATION_ACTIVE ||
        record->generation != handle.generation ||
        !driver_identity_equal(record->owner, owner)) return 0;
    return record;
}

int driver_allocation_release(DriverIdentity owner, uint32_t context,
                              DriverAllocationHandle handle) {
    if (!driver_identity_is_valid(owner) ||
        !driver_allocation_handle_is_valid(handle)) {
        return DRIVER_LOAD_ALLOCATION_DENIED;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_allocation_lock, &token)) {
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    DriverAllocationRecord* record = resolve_locked(owner, handle);
    if (record == 0) {
        DriverAllocationRecord* candidate = handle.slot < DRIVER_MAX_ALLOCATIONS
            ? &g_allocations[handle.slot] : 0;
        if (candidate != 0 && candidate->state == ALLOCATION_ACTIVE &&
            candidate->generation == handle.generation &&
            !driver_identity_equal(candidate->owner, owner)) {
            g_stats.owner_rejections++;
        } else {
            g_stats.stale_rejections++;
        }
        kernel_spinlock_release(&g_allocation_lock, &token);
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    if (!context_allows(context, record->flags)) {
        g_stats.context_rejections++;
        kernel_spinlock_release(&g_allocation_lock, &token);
        return DRIVER_LOAD_CONTEXT_DENIED;
    }
    if (record->pin_count != 0) {
        g_stats.pinned_free_rejections++;
        kernel_spinlock_release(&g_allocation_lock, &token);
        return DRIVER_LOAD_ALLOCATION_DENIED;
    }
    DriverAllocationRecord snapshot = *record;
    record->state = ALLOCATION_RELEASING;
    kernel_spinlock_release(&g_allocation_lock, &token);

    int backing_ok = 1;
    bytes_zero(snapshot.address, snapshot.requested_size);
    if ((snapshot.flags & DRIVER_ALLOC_ATOMIC) != 0) {
        bytes_zero(snapshot.address, DRIVER_ATOMIC_SLOT_SIZE);
    } else if ((snapshot.flags & DRIVER_ALLOC_PAGES) != 0) {
#ifdef OS64_HOST_TEST
        free(snapshot.backing);
#else
        backing_ok = vm_unmap_free_range_tlb_safe(
            (uint64_t)(uintptr_t)snapshot.address,
            snapshot.page_count) == snapshot.page_count;
#endif
    } else {
        kfree(snapshot.backing);
    }
    if (!backing_ok) {
        if (kernel_spinlock_acquire(&g_allocation_lock, &token)) {
            record = &g_allocations[handle.slot];
            record->state = ALLOCATION_QUARANTINED;
            g_stats.quarantined++;
            kernel_spinlock_release(&g_allocation_lock, &token);
        }
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    int resource_result = driver_resource_release(
        owner, snapshot.resource, DRIVER_RESOURCE_ALLOCATION);
    if (resource_result != DRIVER_LOAD_OK) return resource_result;

    if (!kernel_spinlock_acquire(&g_allocation_lock, &token)) {
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    record = &g_allocations[handle.slot];
    if ((snapshot.flags & DRIVER_ALLOC_ATOMIC) != 0 &&
        snapshot.atomic_slot < DRIVER_ATOMIC_SLOT_COUNT) {
        g_atomic_in_use[owner.slot][snapshot.atomic_slot] = 0;
        if (g_stats.atomic_active != 0) g_stats.atomic_active--;
    }
    if ((snapshot.flags & DRIVER_ALLOC_PAGES) != 0) {
        bitmap_set(snapshot.page_index, snapshot.page_count, 0);
    }
    if (g_stats.active != 0) g_stats.active--;
    if (g_stats.active_bytes >= snapshot.charged_size) {
        g_stats.active_bytes -= snapshot.charged_size;
    } else {
        g_stats.active_bytes = 0;
    }
    g_stats.releases++;
    clear_record_locked(record);
    kernel_spinlock_release(&g_allocation_lock, &token);
    return DRIVER_LOAD_OK;
}

int driver_allocation_create_current(uint64_t size, uint64_t alignment,
                                     uint32_t flags, const char* tag,
                                     DriverAllocationResult* out) {
    DriverExecutionContext context;
    if (!driver_execution_current(&context)) return DRIVER_LOAD_CONTEXT_DENIED;
    return driver_allocation_create(context.owner, context.kind, size,
                                    alignment, flags, tag, out);
}

int driver_allocation_release_current(DriverAllocationHandle handle) {
    DriverExecutionContext context;
    if (!driver_execution_current(&context)) return DRIVER_LOAD_CONTEXT_DENIED;
    return driver_allocation_release(context.owner, context.kind, handle);
}

int driver_allocation_pin(DriverIdentity owner, DriverAllocationHandle handle,
                          uint64_t offset, uint64_t length,
                          DriverPinnedAllocation* out) {
    if (out != 0) *out = {};
    if (out == 0 || length == 0 || offset > UINT64_MAX - length)
        return DRIVER_LOAD_BAD_HEADER;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_allocation_lock, &token))
        return DRIVER_LOAD_RESOURCE_DENIED;
    DriverAllocationRecord* record = resolve_locked(owner, handle);
    if (record == 0 || (record->flags & DRIVER_ALLOC_ATOMIC) != 0 ||
        offset > record->requested_size ||
        length > record->requested_size - offset ||
        record->pin_count == UINT32_MAX) {
        kernel_spinlock_release(&g_allocation_lock, &token);
        return DRIVER_LOAD_ALLOCATION_DENIED;
    }
    record->pin_count++;
    g_stats.pinned_ranges++;
    out->address = (void*)((uintptr_t)record->address + offset);
    out->size = length;
    out->flags = record->flags;
    out->pin_count = record->pin_count;
    kernel_spinlock_release(&g_allocation_lock, &token);
    return DRIVER_LOAD_OK;
}

int driver_allocation_unpin(DriverIdentity owner,
                            DriverAllocationHandle handle) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_allocation_lock, &token))
        return DRIVER_LOAD_RESOURCE_DENIED;
    DriverAllocationRecord* record = resolve_locked(owner, handle);
    if (record == 0 || record->pin_count == 0) {
        kernel_spinlock_release(&g_allocation_lock, &token);
        return DRIVER_LOAD_ALLOCATION_DENIED;
    }
    record->pin_count--;
    if (g_stats.pinned_ranges != 0) g_stats.pinned_ranges--;
    kernel_spinlock_release(&g_allocation_lock, &token);
    return DRIVER_LOAD_OK;
}

uint32_t driver_allocation_release_owner(DriverIdentity owner) {
    uint32_t released = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_ALLOCATIONS; i++) {
        KernelSpinlockToken token;
        if (!kernel_spinlock_acquire(&g_allocation_lock, &token)) break;
        DriverAllocationRecord* record = &g_allocations[i];
        DriverAllocationHandle handle = driver_allocation_invalid();
        if (record->state == ALLOCATION_ACTIVE &&
            driver_identity_equal(record->owner, owner)) {
            handle.slot = record->slot;
            handle.generation = record->generation;
        }
        kernel_spinlock_release(&g_allocation_lock, &token);
        if (driver_allocation_handle_is_valid(handle) &&
            driver_allocation_release(owner,
                                      DRIVER_CONTEXT_THREAD_SLEEPABLE,
                                      handle) == DRIVER_LOAD_OK) {
            released++;
            __atomic_add_fetch(&g_stats.automatic_releases, 1u,
                               __ATOMIC_RELAXED);
        }
    }
    return released;
}

uint32_t driver_allocation_owner_count(DriverIdentity owner) {
    if (!driver_identity_is_valid(owner)) return 0;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_allocation_lock, &token)) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_ALLOCATIONS; i++) {
        if (g_allocations[i].state != ALLOCATION_FREE &&
            driver_identity_equal(g_allocations[i].owner, owner)) {
            count++;
        }
    }
    kernel_spinlock_release(&g_allocation_lock, &token);
    return count;
}

void driver_allocation_get_stats(DriverAllocationStats* out) {
    if (out == 0) return;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_allocation_lock, &token)) {
        *out = {};
        return;
    }
    *out = g_stats;
    kernel_spinlock_release(&g_allocation_lock, &token);
}
