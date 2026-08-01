#include "kernel/driver/driver_va.h"

#include "kernel/mm/vm.h"
#include "kernel/spinlock.h"
#include "kernel/fault_injection.h"

extern int kernel_fault_injection_should_fail(uint32_t point)
    __attribute__((weak));

struct DriverVaExtent {
    uint64_t base;
    uint32_t page_count;
};

struct DriverVaRecord {
    uint8_t active;
    uint8_t quarantined;
    uint16_t guard_pages;
    uint32_t slot;
    uint32_t generation;
    DriverIdentity owner;
    uint64_t reserved_base;
    uint64_t usable_base;
    uint32_t reserved_pages;
    uint32_t usable_pages;
};

static DriverVaExtent g_free_extents[DRIVER_IMAGE_VA_MAX_EXTENTS];
static DriverVaRecord g_allocations[DRIVER_IMAGE_VA_MAX_ALLOCATIONS];
static DriverVaStats g_stats;
static uint64_t g_arena_base;
static uint64_t g_arena_limit;
static uint64_t g_highest_reserved_end;
static uint32_t g_guard_pages;
static uint32_t g_free_extent_count;
static uint8_t g_initialized;
static KernelSpinlock g_va_lock =
    KERNEL_SPINLOCK_INITIALIZER(KERNEL_LOCK_CLASS_VFS_DEVICE, "driver_image_va");

static uint32_t next_generation(uint32_t generation) {
    generation++;
    return generation != 0 ? generation : 1u;
}

static uint64_t extent_end(const DriverVaExtent* extent) {
    return extent->base + (uint64_t)extent->page_count * VM_PAGE_SIZE;
}

static void refresh_free_stats_locked() {
    g_stats.free_pages = 0;
    g_stats.largest_free_pages = 0;
    g_stats.free_extents = g_free_extent_count;
    for (uint32_t i = 0; i < g_free_extent_count; i++) {
        g_stats.free_pages += g_free_extents[i].page_count;
        if (g_free_extents[i].page_count > g_stats.largest_free_pages) {
            g_stats.largest_free_pages = g_free_extents[i].page_count;
        }
    }
}

static int initialize_arena(uint64_t base, uint64_t limit,
                            uint32_t guard_pages) {
    if ((base & (VM_PAGE_SIZE - 1ULL)) != 0 ||
        (limit & (VM_PAGE_SIZE - 1ULL)) != 0 || limit <= base) {
        return 0;
    }
    const uint64_t pages = (limit - base) / VM_PAGE_SIZE;
    if (pages == 0 || pages > 0xFFFFFFFFULL ||
        guard_pages > 0x7FFFu) {
        return 0;
    }
    kernel_spinlock_init(&g_va_lock, KERNEL_LOCK_CLASS_VFS_DEVICE,
                         "driver_image_va");
    g_arena_base = base;
    g_arena_limit = limit;
    g_highest_reserved_end = base;
    g_guard_pages = guard_pages;
    g_free_extent_count = 1;
    g_free_extents[0].base = base;
    g_free_extents[0].page_count = (uint32_t)pages;
    for (uint32_t i = 1; i < DRIVER_IMAGE_VA_MAX_EXTENTS; i++) {
        g_free_extents[i] = {};
    }
    for (uint32_t i = 0; i < DRIVER_IMAGE_VA_MAX_ALLOCATIONS; i++) {
        const uint32_t generation = g_allocations[i].generation;
        g_allocations[i] = {};
        g_allocations[i].slot = i;
        g_allocations[i].generation = generation;
    }
    g_stats = {};
    g_stats.arena_pages = pages;
    refresh_free_stats_locked();
    g_initialized = 1;
    return 1;
}

void driver_image_va_init() {
    initialize_arena(DRIVER_IMAGE_VA_BASE, DRIVER_IMAGE_VA_LIMIT,
                     DRIVER_IMAGE_VA_GUARD_PAGES);
}

DriverVaHandle driver_image_va_invalid() {
    DriverVaHandle handle = {DRIVER_IDENTITY_INVALID_SLOT, 0};
    return handle;
}

int driver_image_va_handle_is_valid(DriverVaHandle handle) {
    return handle.slot < DRIVER_IMAGE_VA_MAX_ALLOCATIONS &&
           handle.generation != 0;
}

static DriverVaRecord* resolve_locked(DriverIdentity owner,
                                      DriverVaHandle handle) {
    if (!driver_image_va_handle_is_valid(handle)) return 0;
    DriverVaRecord* record = &g_allocations[handle.slot];
    if (!record->active || record->generation != handle.generation ||
        !driver_identity_equal(record->owner, owner)) {
        return 0;
    }
    return record;
}

int driver_image_va_allocate(DriverIdentity owner,
                             uint32_t page_count,
                             DriverVaHandle* out_handle,
                             uint64_t* out_base) {
    if (out_handle != 0) *out_handle = driver_image_va_invalid();
    if (out_base != 0) *out_base = 0;
    if (!g_initialized || !driver_identity_is_valid(owner) || page_count == 0 ||
        out_handle == 0 || out_base == 0) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    if (!driver_manager_identity_is_live(owner)) {
        return DRIVER_LOAD_STALE_IDENTITY;
    }
    if (!driver_manager_identity_accepts_resources(owner)) {
        return DRIVER_LOAD_STATE_DENIED;
    }
    const uint64_t reserved64 = (uint64_t)page_count +
                                (uint64_t)g_guard_pages * 2ULL;
    if (reserved64 > 0xFFFFFFFFULL) return DRIVER_LOAD_OUT_OF_MEMORY;

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_va_lock, &token)) {
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    DriverVaRecord* record = 0;
    if (kernel_fault_injection_should_fail == 0 ||
        !kernel_fault_injection_should_fail(
            KERNEL_FAULT_POINT_DRIVER_VA_RECORD)) {
        for (uint32_t i = 0; i < DRIVER_IMAGE_VA_MAX_ALLOCATIONS; i++) {
            if (!g_allocations[i].active) {
                record = &g_allocations[i];
                break;
            }
        }
    }
    uint32_t extent_index = DRIVER_IMAGE_VA_MAX_EXTENTS;
    for (uint32_t i = 0; record != 0 && i < g_free_extent_count; i++) {
        if (g_free_extents[i].page_count >= reserved64) {
            extent_index = i;
            break;
        }
    }
    if (record == 0 || extent_index == DRIVER_IMAGE_VA_MAX_EXTENTS) {
        g_stats.exhaustion_failures++;
        kernel_spinlock_release(&g_va_lock, &token);
        return DRIVER_LOAD_OUT_OF_MEMORY;
    }

    DriverVaExtent* extent = &g_free_extents[extent_index];
    const uint64_t reserved_base = extent->base;
    extent->base += reserved64 * VM_PAGE_SIZE;
    extent->page_count -= (uint32_t)reserved64;
    if (extent->page_count == 0) {
        for (uint32_t i = extent_index + 1; i < g_free_extent_count; i++) {
            g_free_extents[i - 1] = g_free_extents[i];
        }
        g_free_extent_count--;
    }

    record->generation = next_generation(record->generation);
    record->active = 1;
    record->quarantined = 0;
    record->guard_pages = (uint16_t)g_guard_pages;
    record->owner = owner;
    record->reserved_base = reserved_base;
    record->usable_base = reserved_base +
                          (uint64_t)g_guard_pages * VM_PAGE_SIZE;
    record->reserved_pages = (uint32_t)reserved64;
    record->usable_pages = page_count;
    out_handle->slot = record->slot;
    out_handle->generation = record->generation;
    *out_base = record->usable_base;
    g_stats.active++;
    g_stats.allocations++;
    g_stats.guard_pages += (uint64_t)g_guard_pages * 2ULL;
    if (g_stats.active > g_stats.high_water) g_stats.high_water = g_stats.active;
    const uint64_t reserved_end = reserved_base + reserved64 * VM_PAGE_SIZE;
    if (reserved_base < g_highest_reserved_end) {
        g_stats.reused_allocations++;
    }
    if (reserved_end > g_highest_reserved_end) {
        g_highest_reserved_end = reserved_end;
    }
    refresh_free_stats_locked();
    kernel_spinlock_release(&g_va_lock, &token);
    return DRIVER_LOAD_OK;
}

static int insert_extent_locked(uint64_t base, uint32_t page_count) {
    if (page_count == 0 || g_free_extent_count >= DRIVER_IMAGE_VA_MAX_EXTENTS) {
        return 0;
    }
    const uint64_t size = (uint64_t)page_count * VM_PAGE_SIZE;
    if (base < g_arena_base || base > g_arena_limit ||
        size > g_arena_limit - base) {
        return 0;
    }
    uint32_t index = 0;
    while (index < g_free_extent_count && g_free_extents[index].base < base) {
        index++;
    }
    if ((index > 0 && extent_end(&g_free_extents[index - 1]) > base) ||
        (index < g_free_extent_count && base + size > g_free_extents[index].base)) {
        return 0;
    }
    for (uint32_t i = g_free_extent_count; i > index; i--) {
        g_free_extents[i] = g_free_extents[i - 1];
    }
    g_free_extents[index].base = base;
    g_free_extents[index].page_count = page_count;
    g_free_extent_count++;

    if (index > 0 && extent_end(&g_free_extents[index - 1]) ==
                         g_free_extents[index].base) {
        g_free_extents[index - 1].page_count +=
            g_free_extents[index].page_count;
        for (uint32_t i = index + 1; i < g_free_extent_count; i++) {
            g_free_extents[i - 1] = g_free_extents[i];
        }
        g_free_extent_count--;
        index--;
    }
    if (index + 1 < g_free_extent_count &&
        extent_end(&g_free_extents[index]) == g_free_extents[index + 1].base) {
        g_free_extents[index].page_count += g_free_extents[index + 1].page_count;
        for (uint32_t i = index + 2; i < g_free_extent_count; i++) {
            g_free_extents[i - 1] = g_free_extents[i];
        }
        g_free_extent_count--;
    }
    return 1;
}

int driver_image_va_release(DriverIdentity owner, DriverVaHandle handle) {
    if (!driver_identity_is_valid(owner) ||
        !driver_image_va_handle_is_valid(handle)) {
        return DRIVER_LOAD_BAD_HEADER;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_va_lock, &token)) {
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    DriverVaRecord* record = resolve_locked(owner, handle);
    if (record == 0) {
        DriverVaRecord* candidate = handle.slot < DRIVER_IMAGE_VA_MAX_ALLOCATIONS
            ? &g_allocations[handle.slot] : 0;
        if (candidate != 0 && candidate->active &&
            candidate->generation == handle.generation &&
            !driver_identity_equal(candidate->owner, owner)) {
            g_stats.owner_rejections++;
        } else {
            g_stats.stale_rejections++;
        }
        kernel_spinlock_release(&g_va_lock, &token);
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    if (record->quarantined ||
        !insert_extent_locked(record->reserved_base, record->reserved_pages)) {
        kernel_spinlock_release(&g_va_lock, &token);
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    record->active = 0;
    record->owner = driver_identity_invalid();
    record->reserved_base = 0;
    record->usable_base = 0;
    record->reserved_pages = 0;
    record->usable_pages = 0;
    if (g_stats.active != 0) g_stats.active--;
    g_stats.releases++;
    refresh_free_stats_locked();
    kernel_spinlock_release(&g_va_lock, &token);
    return DRIVER_LOAD_OK;
}

int driver_image_va_quarantine(DriverIdentity owner, DriverVaHandle handle) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_va_lock, &token)) {
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    DriverVaRecord* record = resolve_locked(owner, handle);
    if (record == 0) {
        kernel_spinlock_release(&g_va_lock, &token);
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    if (!record->quarantined) {
        record->quarantined = 1;
        g_stats.quarantined++;
    }
    kernel_spinlock_release(&g_va_lock, &token);
    return DRIVER_LOAD_OK;
}

void driver_image_va_get_stats(DriverVaStats* out) {
    if (out == 0) return;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_va_lock, &token)) {
        *out = {};
        return;
    }
    *out = g_stats;
    kernel_spinlock_release(&g_va_lock, &token);
}

#ifdef OS64_HOST_TEST
int driver_image_va_reset_for_test(uint64_t base, uint64_t limit,
                                   uint32_t guard_pages) {
    return initialize_arena(base, limit, guard_pages);
}
#endif
