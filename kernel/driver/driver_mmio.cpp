#include "kernel/driver/driver_mmio.h"
#include "kernel/driver/driver_alloc.h"
#include "kernel/driver/drv_format.h"
#include "kernel/mm/vm.h"
#include "kernel/pci.h"
#include "kernel/spinlock.h"

#define MMIO_ARENA_PAGES ((DRIVER_MMIO_ARENA_LIMIT - DRIVER_MMIO_ARENA_BASE) / VM_PAGE_SIZE)

struct MmioRecord {
    uint8_t active;
    uint8_t quarantined;
    uint16_t references;
    uint32_t generation;
    DriverIdentity owner;
    DriverDeviceIdentity device;
    DriverResourceHandle resource;
    uint32_t bar_index;
    uint32_t cache_policy;
    uint32_t first_page;
    uint32_t page_count;
    uint64_t page_offset;
    uint64_t logical_offset;
    uint64_t length;
    uint64_t virtual_base;
};

static MmioRecord g_mmio[DRIVER_MAX_MMIO_MAPPINGS];
static uint64_t g_mmio_pages[(MMIO_ARENA_PAGES + 63u) / 64u];
static uint64_t g_mmio_quarantine[(MMIO_ARENA_PAGES + 63u) / 64u];
static DriverMmioStats g_mmio_stats;
static KernelSpinlock g_mmio_lock =
    KERNEL_SPINLOCK_INITIALIZER(KERNEL_LOCK_CLASS_ADDRESS_SPACE, "driver_mmio");
#ifdef OS64_DRIVER_HOST_TEST
static uint8_t g_host_mmio[DRIVER_MMIO_ARENA_LIMIT - DRIVER_MMIO_ARENA_BASE];
#endif

static uint32_t next_generation(uint32_t value) {
    value++;
    return value != 0 ? value : 1u;
}

static int bit_get(const uint64_t* bits, uint32_t page) {
    return (bits[page / 64u] >> (page % 64u)) & 1u;
}

static void bit_set(uint64_t* bits, uint32_t page, int value) {
    uint64_t mask = 1ULL << (page % 64u);
    if (value) bits[page / 64u] |= mask;
    else bits[page / 64u] &= ~mask;
}

static int reserve_pages(uint32_t count, uint32_t* out) {
    if (count == 0 || count > MMIO_ARENA_PAGES) return 0;
    uint32_t run = 0;
    for (uint32_t page = 0; page < MMIO_ARENA_PAGES; page++) {
        if (!bit_get(g_mmio_pages, page) &&
            !bit_get(g_mmio_quarantine, page)) run++;
        else run = 0;
        if (run == count) {
            *out = page + 1u - count;
            for (uint32_t i = 0; i < count; i++)
                bit_set(g_mmio_pages, *out + i, 1);
            return 1;
        }
    }
    return 0;
}

static void release_pages(uint32_t first, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) bit_set(g_mmio_pages, first + i, 0);
}

static const PCIDeviceInfo* resolve_pci(const DriverBindingRecord* binding) {
    const uint32_t count = pci_get_device_count();
    for (uint32_t i = 0; i < count; i++) {
        const PCIDeviceInfo* pci = pci_get_device(i);
        if (pci != 0 && pci->bus == binding->bus &&
            pci->device == binding->device &&
            pci->function == binding->function) return pci;
    }
    return 0;
}

static int owner_has_permissions(DriverIdentity owner) {
    const uint32_t count = driver_manager_count();
    for (uint32_t i = 0; i < count; i++) {
        const DriverRecord* record = driver_manager_get(i);
        if (record != 0 && record->active && record->slot == owner.slot &&
            record->generation == owner.generation)
            return (record->permissions &
                    (DRV_PERMISSION_PCI | DRV_PERMISSION_MMIO)) ==
                   (DRV_PERMISSION_PCI | DRV_PERMISSION_MMIO);
    }
    return 0;
}

void driver_mmio_init() {
    kernel_spinlock_init(&g_mmio_lock, KERNEL_LOCK_CLASS_ADDRESS_SPACE,
                         "driver_mmio");
    for (uint32_t i = 0; i < DRIVER_MAX_MMIO_MAPPINGS; i++) {
        g_mmio[i] = {};
        g_mmio[i].generation = 0;
    }
    for (uint32_t i = 0; i < (MMIO_ARENA_PAGES + 63u) / 64u; i++) {
        g_mmio_pages[i] = 0;
        g_mmio_quarantine[i] = 0;
    }
    g_mmio_stats = {};
    g_mmio_stats.arena_pages = MMIO_ARENA_PAGES;
    g_mmio_stats.free_pages = MMIO_ARENA_PAGES;
}

DriverMmioHandle driver_mmio_invalid() {
    DriverMmioHandle value = {DRIVER_IDENTITY_INVALID_SLOT, 0};
    return value;
}

static MmioRecord* resolve_locked(DriverIdentity owner, DriverMmioHandle handle) {
    if (handle.slot >= DRIVER_MAX_MMIO_MAPPINGS || handle.generation == 0)
        return 0;
    MmioRecord* record = &g_mmio[handle.slot];
    if (!record->active || record->generation != handle.generation) return 0;
    if (!driver_identity_equal(record->owner, owner)) return 0;
    return record;
}

int driver_mmio_map(DriverIdentity owner, DriverDeviceIdentity device,
                    uint32_t bar_index, uint64_t offset, uint64_t length,
                    uint32_t cache_policy, DriverMmioMapping* out) {
    if (out != 0) { out->handle = driver_mmio_invalid(); out->length = 0; }
    if (out == 0 || length == 0 || offset > UINT64_MAX - length) {
        g_mmio_stats.range_rejections++;
        return DRIVER_LOAD_MMIO_RANGE;
    }
    if (!driver_manager_identity_accepts_resources(owner) ||
        !owner_has_permissions(owner)) return DRIVER_LOAD_MMIO_DENIED;
    if (cache_policy != DRIVER_MMIO_CACHE_DEVICE_UC) {
        g_mmio_stats.cache_rejections++;
        return DRIVER_LOAD_MMIO_CACHE;
    }
    DriverBindingRecord binding;
    if (!driver_manager_device_identity_resolve(device, owner, &binding) ||
        binding.kind != DRIVER_BIND_KIND_PCI) return DRIVER_LOAD_MMIO_DENIED;
    const PCIDeviceInfo* pci = resolve_pci(&binding);
    PCIBarInfo bar;
    if (pci == 0 || !pci_get_bar(pci, bar_index, &bar) ||
        bar.type == PCI_BAR_TYPE_IO || bar.base == 0 || bar.size == 0 ||
        offset > bar.size || length > bar.size - offset) {
        g_mmio_stats.range_rejections++;
        return DRIVER_LOAD_MMIO_RANGE;
    }

    const uint64_t physical = bar.base + offset;
    const uint64_t page_offset = physical & (VM_PAGE_SIZE - 1u);
    const uint64_t bytes = page_offset + length;
    if (bytes < length || bytes > UINT32_MAX * VM_PAGE_SIZE)
        return DRIVER_LOAD_MMIO_RANGE;
    const uint32_t page_count = (uint32_t)((bytes + VM_PAGE_SIZE - 1u) /
                                           VM_PAGE_SIZE);

    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_mmio_lock, &token))
        return DRIVER_LOAD_MMIO_DENIED;
    for (uint32_t i = 0; i < DRIVER_MAX_MMIO_MAPPINGS; i++) {
        MmioRecord* record = &g_mmio[i];
        if (record->active && driver_identity_equal(record->owner, owner) &&
            record->device.slot == device.slot &&
            record->device.generation == device.generation &&
            record->bar_index == bar_index &&
            record->logical_offset == offset && record->length == length &&
            record->cache_policy == cache_policy) {
            if (record->references == UINT16_MAX) {
                kernel_spinlock_release(&g_mmio_lock, &token);
                return DRIVER_LOAD_NO_SLOT;
            }
            record->references++;
            g_mmio_stats.shared_maps++;
            out->handle = {i, record->generation};
            out->length = length;
            kernel_spinlock_release(&g_mmio_lock, &token);
            return DRIVER_LOAD_OK;
        }
    }
    uint32_t slot = DRIVER_MAX_MMIO_MAPPINGS;
    for (uint32_t i = 0; i < DRIVER_MAX_MMIO_MAPPINGS; i++)
        if (!g_mmio[i].active && !g_mmio[i].quarantined) { slot = i; break; }
    uint32_t first_page = 0;
    if (slot == DRIVER_MAX_MMIO_MAPPINGS ||
        !reserve_pages(page_count, &first_page)) {
        kernel_spinlock_release(&g_mmio_lock, &token);
        return DRIVER_LOAD_NO_SLOT;
    }
    MmioRecord* record = &g_mmio[slot];
    const uint32_t generation = next_generation(record->generation);
    *record = {};
    record->generation = generation;
    record->owner = owner;
    record->device = device;
    record->bar_index = bar_index;
    record->cache_policy = cache_policy;
    record->first_page = first_page;
    record->page_count = page_count;
    record->page_offset = page_offset;
    record->logical_offset = offset;
    record->length = length;
#ifdef OS64_DRIVER_HOST_TEST
    record->virtual_base = (uint64_t)(uintptr_t)(g_host_mmio +
        (uint64_t)first_page * VM_PAGE_SIZE);
#else
    record->virtual_base = DRIVER_MMIO_ARENA_BASE +
        (uint64_t)first_page * VM_PAGE_SIZE;
    const uint64_t aligned_physical = physical - page_offset;
    uint32_t mapped = 0;
    for (; mapped < page_count; mapped++) {
        if (!vm_map_page(record->virtual_base + (uint64_t)mapped * VM_PAGE_SIZE,
                         aligned_physical + (uint64_t)mapped * VM_PAGE_SIZE,
                         VM_FLAG_WRITABLE | VM_FLAG_CACHE_DISABLE |
                         VM_FLAG_WRITE_THROUGH | VM_FLAG_NO_EXECUTE)) break;
    }
    if (mapped != page_count) {
        for (uint32_t i = 0; i < mapped; i++)
            vm_unmap_page(record->virtual_base + (uint64_t)i * VM_PAGE_SIZE);
        release_pages(first_page, page_count);
        *record = {}; record->generation = generation;
        kernel_spinlock_release(&g_mmio_lock, &token);
        return DRIVER_LOAD_OUT_OF_MEMORY;
    }
#endif
    if (!pci_enable_memory_space(pci)) {
#ifndef OS64_DRIVER_HOST_TEST
        for (uint32_t i = 0; i < page_count; i++)
            vm_unmap_page(record->virtual_base + (uint64_t)i * VM_PAGE_SIZE);
#endif
        release_pages(first_page, page_count);
        *record = {}; record->generation = generation;
        kernel_spinlock_release(&g_mmio_lock, &token);
        return DRIVER_LOAD_MMIO_DENIED;
    }
    int resource_result = driver_resource_register(owner, device,
        DRIVER_RESOURCE_MMIO, cache_policy, bar_index, length, "pci_mmio",
        &record->resource);
    if (resource_result != DRIVER_LOAD_OK) {
#ifndef OS64_DRIVER_HOST_TEST
        for (uint32_t i = 0; i < page_count; i++)
            vm_unmap_page(record->virtual_base + (uint64_t)i * VM_PAGE_SIZE);
#endif
        release_pages(first_page, page_count);
        *record = {}; record->generation = generation;
        kernel_spinlock_release(&g_mmio_lock, &token);
        return resource_result;
    }
    record->references = 1;
    record->active = 1;
    g_mmio_stats.active++;
    if (g_mmio_stats.active > g_mmio_stats.high_water)
        g_mmio_stats.high_water = g_mmio_stats.active;
    g_mmio_stats.free_pages -= page_count;
    g_mmio_stats.maps++;
    out->handle = {slot, generation};
    out->length = length;
    kernel_spinlock_release(&g_mmio_lock, &token);
    return DRIVER_LOAD_OK;
}

int driver_mmio_map_current(DriverDeviceIdentity device, uint32_t bar_index,
                            uint64_t offset, uint64_t length,
                            uint32_t cache_policy, DriverMmioMapping* out) {
    DriverExecutionContext context;
    if (!driver_execution_current(&context) ||
        context.kind != DRIVER_CONTEXT_THREAD_SLEEPABLE)
        return DRIVER_LOAD_CONTEXT_DENIED;
    return driver_mmio_map(context.owner, device, bar_index, offset, length,
                           cache_policy, out);
}

int driver_mmio_unmap(DriverIdentity owner, DriverMmioHandle handle) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_mmio_lock, &token))
        return DRIVER_LOAD_MMIO_DENIED;
    MmioRecord* record = resolve_locked(owner, handle);
    if (record == 0) {
        if (handle.slot < DRIVER_MAX_MMIO_MAPPINGS &&
            g_mmio[handle.slot].active &&
            g_mmio[handle.slot].generation == handle.generation)
            g_mmio_stats.owner_rejections++;
        else g_mmio_stats.stale_rejections++;
        kernel_spinlock_release(&g_mmio_lock, &token);
        return DRIVER_LOAD_MMIO_DENIED;
    }
    if (--record->references != 0) {
        kernel_spinlock_release(&g_mmio_lock, &token);
        return DRIVER_LOAD_OK;
    }
    MmioRecord snapshot = *record;
    record->active = 0;
    record->quarantined = 1;
    g_mmio_stats.active--;
    kernel_spinlock_release(&g_mmio_lock, &token);
#ifndef OS64_DRIVER_HOST_TEST
    if (vm_unmap_range_tlb_safe(snapshot.virtual_base, snapshot.page_count) !=
        snapshot.page_count) {
        if (!kernel_spinlock_acquire(&g_mmio_lock, &token))
            return DRIVER_LOAD_RESOURCE_DENIED;
        for (uint32_t i = 0; i < snapshot.page_count; i++) {
            bit_set(g_mmio_pages, snapshot.first_page + i, 0);
            bit_set(g_mmio_quarantine, snapshot.first_page + i, 1);
        }
        g_mmio_stats.quarantined_pages += snapshot.page_count;
        kernel_spinlock_release(&g_mmio_lock, &token);
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
#endif
    driver_resource_release(owner, snapshot.resource, DRIVER_RESOURCE_MMIO);
    if (!kernel_spinlock_acquire(&g_mmio_lock, &token))
        return DRIVER_LOAD_RESOURCE_DENIED;
    release_pages(snapshot.first_page, snapshot.page_count);
    g_mmio_stats.free_pages += snapshot.page_count;
    g_mmio_stats.unmaps++;
    const uint32_t generation = record->generation;
    *record = {};
    record->generation = generation;
    kernel_spinlock_release(&g_mmio_lock, &token);
    return DRIVER_LOAD_OK;
}

int driver_mmio_unmap_current(DriverMmioHandle handle) {
    DriverExecutionContext context;
    if (!driver_execution_current(&context)) return DRIVER_LOAD_CONTEXT_DENIED;
    return driver_mmio_unmap(context.owner, handle);
}

static int validate_access(MmioRecord* record, uint64_t offset, uint32_t width) {
    if (width != 1 && width != 2 && width != 4 && width != 8)
        return DRIVER_LOAD_MMIO_RANGE;
    if ((offset & (width - 1u)) != 0 || offset > record->length ||
        width > record->length - offset) return DRIVER_LOAD_MMIO_RANGE;
    return DRIVER_LOAD_OK;
}

int driver_mmio_read(DriverIdentity owner, DriverMmioHandle handle,
                     uint64_t offset, uint32_t width, uint64_t* out) {
    if (out == 0) return DRIVER_LOAD_BAD_HEADER;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_mmio_lock, &token))
        return DRIVER_LOAD_MMIO_DENIED;
    MmioRecord* record = resolve_locked(owner, handle);
    if (record == 0) {
        g_mmio_stats.stale_rejections++;
        kernel_spinlock_release(&g_mmio_lock, &token);
        return DRIVER_LOAD_MMIO_DENIED;
    }
    int result = validate_access(record, offset, width);
    if (result == DRIVER_LOAD_OK) {
        uintptr_t address = (uintptr_t)(record->virtual_base +
                                        record->page_offset + offset);
        if (width == 1) *out = *(volatile uint8_t*)address;
        else if (width == 2) *out = *(volatile uint16_t*)address;
        else if (width == 4) *out = *(volatile uint32_t*)address;
        else *out = *(volatile uint64_t*)address;
    } else g_mmio_stats.range_rejections++;
    kernel_spinlock_release(&g_mmio_lock, &token);
    return result;
}

int driver_mmio_write(DriverIdentity owner, DriverMmioHandle handle,
                      uint64_t offset, uint32_t width, uint64_t value) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_mmio_lock, &token))
        return DRIVER_LOAD_MMIO_DENIED;
    MmioRecord* record = resolve_locked(owner, handle);
    if (record == 0) {
        g_mmio_stats.stale_rejections++;
        kernel_spinlock_release(&g_mmio_lock, &token);
        return DRIVER_LOAD_MMIO_DENIED;
    }
    int result = validate_access(record, offset, width);
    if (result == DRIVER_LOAD_OK) {
        uintptr_t address = (uintptr_t)(record->virtual_base +
                                        record->page_offset + offset);
        if (width == 1) *(volatile uint8_t*)address = (uint8_t)value;
        else if (width == 2) *(volatile uint16_t*)address = (uint16_t)value;
        else if (width == 4) *(volatile uint32_t*)address = (uint32_t)value;
        else *(volatile uint64_t*)address = value;
    } else g_mmio_stats.range_rejections++;
    kernel_spinlock_release(&g_mmio_lock, &token);
    return result;
}

int driver_mmio_read_current(DriverMmioHandle handle, uint64_t offset,
                             uint32_t width, uint64_t* out) {
    DriverExecutionContext context;
    if (!driver_execution_current(&context)) return DRIVER_LOAD_CONTEXT_DENIED;
    return driver_mmio_read(context.owner, handle, offset, width, out);
}

int driver_mmio_write_current(DriverMmioHandle handle, uint64_t offset,
                              uint32_t width, uint64_t value) {
    DriverExecutionContext context;
    if (!driver_execution_current(&context)) return DRIVER_LOAD_CONTEXT_DENIED;
    return driver_mmio_write(context.owner, handle, offset, width, value);
}

int driver_mmio_barrier_current(DriverMmioHandle handle, uint32_t direction) {
    DriverExecutionContext context;
    if (!driver_execution_current(&context)) return DRIVER_LOAD_CONTEXT_DENIED;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_mmio_lock, &token))
        return DRIVER_LOAD_MMIO_DENIED;
    MmioRecord* record = resolve_locked(context.owner, handle);
    if (record == 0 || direction < DRIVER_MMIO_BARRIER_READ ||
        direction > DRIVER_MMIO_BARRIER_FULL) {
        kernel_spinlock_release(&g_mmio_lock, &token);
        return DRIVER_LOAD_MMIO_DENIED;
    }
    __atomic_thread_fence(direction == DRIVER_MMIO_BARRIER_READ
        ? __ATOMIC_ACQUIRE : direction == DRIVER_MMIO_BARRIER_WRITE
        ? __ATOMIC_RELEASE : __ATOMIC_SEQ_CST);
    kernel_spinlock_release(&g_mmio_lock, &token);
    return DRIVER_LOAD_OK;
}

uint32_t driver_mmio_release_owner(DriverIdentity owner) {
    uint32_t released = 0;
    for (;;) {
        DriverMmioHandle handle = driver_mmio_invalid();
        KernelSpinlockToken token;
        if (!kernel_spinlock_acquire(&g_mmio_lock, &token)) break;
        for (uint32_t i = 0; i < DRIVER_MAX_MMIO_MAPPINGS; i++) {
            if (g_mmio[i].active &&
                driver_identity_equal(g_mmio[i].owner, owner)) {
                g_mmio[i].references = 1;
                handle = {i, g_mmio[i].generation};
                break;
            }
        }
        kernel_spinlock_release(&g_mmio_lock, &token);
        if (handle.slot == DRIVER_IDENTITY_INVALID_SLOT) break;
        if (driver_mmio_unmap(owner, handle) != DRIVER_LOAD_OK) break;
        released++;
    }
    return released;
}

uint32_t driver_mmio_owner_count(DriverIdentity owner) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_mmio_lock, &token)) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_MMIO_MAPPINGS; i++)
        if ((g_mmio[i].active || g_mmio[i].quarantined) &&
            driver_identity_equal(g_mmio[i].owner, owner)) count++;
    kernel_spinlock_release(&g_mmio_lock, &token);
    return count;
}

void driver_mmio_get_stats(DriverMmioStats* out) {
    if (out == 0) return;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_mmio_lock, &token)) { *out = {}; return; }
    *out = g_mmio_stats;
    kernel_spinlock_release(&g_mmio_lock, &token);
}
