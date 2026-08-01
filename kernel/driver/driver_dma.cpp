#include "kernel/driver/driver_dma.h"
#include "kernel/driver/driver_alloc.h"
#include "kernel/driver/drv_format.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vm.h"
#include "kernel/pci.h"
#include "kernel/spinlock.h"
#ifdef OS64_DRIVER_HOST_TEST
#include <stdlib.h>
#endif

#define DMA_ARENA_PAGES ((DRIVER_DMA_ARENA_LIMIT - DRIVER_DMA_ARENA_BASE) / VM_PAGE_SIZE)

struct DmaDomainRecord {
    uint8_t active;
    uint8_t backend;
    uint8_t isolated;
    uint8_t bus_mastering;
    uint32_t generation;
    DriverIdentity owner;
    DriverDeviceIdentity device;
    uint32_t mask_bits;
    uint32_t mapping_count;
};

struct DmaBufferRecord {
    uint8_t active;
    uint8_t quarantined;
    uint16_t reserved;
    uint32_t generation;
    DriverIdentity owner;
    DriverDeviceIdentity device;
    DriverResourceHandle resource;
    uint32_t domain_slot;
    uint32_t first_page;
    uint32_t page_count;
    uint32_t reserved2;
    void* cpu_address;
    uint64_t physical_address;
    DriverDmaAddress dma_address;
    uint64_t logical_size;
    uint64_t charged_size;
};

struct DmaMappingRecord {
    uint8_t active;
    uint8_t direction;
    uint8_t sync_state;
    uint8_t source_count;
    uint32_t generation;
    DriverIdentity owner;
    DriverDeviceIdentity device;
    DriverResourceHandle resource;
    uint32_t domain_slot;
    uint32_t segment_count;
    DriverAllocationHandle sources[DRIVER_DMA_MAX_SOURCES];
    void* source_addresses[DRIVER_DMA_MAX_SOURCES];
    uint64_t source_lengths[DRIVER_DMA_MAX_SOURCES];
    DriverDmaSegment segments[DRIVER_DMA_MAX_SEGMENTS];
    DriverDmaHandle bounce;
    void* bounce_cpu;
    uint64_t bounce_size;
};

static DmaDomainRecord g_domains[DRIVER_MAX_DMA_DOMAINS];
static DmaBufferRecord g_buffers[DRIVER_MAX_DMA_BUFFERS];
static DmaMappingRecord g_mappings[DRIVER_MAX_DMA_MAPPINGS];
static uint64_t g_dma_va[(DMA_ARENA_PAGES + 63u) / 64u];
static DriverDmaStats g_dma_stats;
static KernelSpinlock g_dma_lock =
    KERNEL_SPINLOCK_INITIALIZER(KERNEL_LOCK_CLASS_ADDRESS_SPACE, "driver_dma");
#ifdef OS64_DRIVER_HOST_TEST
static uint64_t g_host_next_dma = 0x00200000ULL;
#endif

static uint32_t next_generation(uint32_t value) {
    value++;
    return value != 0 ? value : 1u;
}
static int power_of_two(uint64_t value) {
    return value != 0 && (value & (value - 1u)) == 0;
}
static uint64_t align_up(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}
static int bit_get(uint32_t page) {
    return (g_dma_va[page / 64u] >> (page % 64u)) & 1u;
}
static void bit_set(uint32_t page, int value) {
    uint64_t mask = 1ULL << (page % 64u);
    if (value) g_dma_va[page / 64u] |= mask;
    else g_dma_va[page / 64u] &= ~mask;
}
static int reserve_va(uint32_t count, uint32_t* out) {
    uint32_t run = 0;
    for (uint32_t page = 0; page < DMA_ARENA_PAGES; page++) {
        run = bit_get(page) ? 0 : run + 1;
        if (run == count) {
            *out = page + 1u - count;
            for (uint32_t i = 0; i < count; i++) bit_set(*out + i, 1);
            return 1;
        }
    }
    return 0;
}
static void release_va(uint32_t first, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) bit_set(first + i, 0);
}
static uint64_t mask_limit(uint32_t bits) {
    return bits == 64 ? UINT64_MAX : ((1ULL << bits) - 1u);
}
static int owner_has_dma(DriverIdentity owner) {
    for (uint32_t i = 0; i < driver_manager_count(); i++) {
        const DriverRecord* record = driver_manager_get(i);
        if (record && record->active && record->slot == owner.slot &&
            record->generation == owner.generation)
            return (record->permissions & (DRV_PERMISSION_PCI |
                    DRV_PERMISSION_DMA)) ==
                   (DRV_PERMISSION_PCI | DRV_PERMISSION_DMA);
    }
    return 0;
}
static DmaDomainRecord* domain_for(DriverIdentity owner,
                                   DriverDeviceIdentity device) {
    for (uint32_t i = 0; i < DRIVER_MAX_DMA_DOMAINS; i++)
        if (g_domains[i].active &&
            driver_identity_equal(g_domains[i].owner, owner) &&
            g_domains[i].device.slot == device.slot &&
            g_domains[i].device.generation == device.generation)
            return &g_domains[i];
    return 0;
}
static const PCIDeviceInfo* resolve_pci(DriverIdentity owner,
                                        DriverDeviceIdentity device) {
    DriverBindingRecord binding;
    if (!driver_manager_device_identity_resolve(device, owner, &binding))
        return 0;
    for (uint32_t i = 0; i < pci_get_device_count(); i++) {
        const PCIDeviceInfo* pci = pci_get_device(i);
        if (pci && pci->bus == binding.bus && pci->device == binding.device &&
            pci->function == binding.function) return pci;
    }
    return 0;
}

void driver_dma_init() {
    kernel_spinlock_init(&g_dma_lock, KERNEL_LOCK_CLASS_ADDRESS_SPACE,
                         "driver_dma");
    for (uint32_t i = 0; i < DRIVER_MAX_DMA_DOMAINS; i++) {
        uint32_t generation = g_domains[i].generation;
        g_domains[i] = {}; g_domains[i].generation = generation;
    }
    for (uint32_t i = 0; i < DRIVER_MAX_DMA_BUFFERS; i++) {
        uint32_t generation = g_buffers[i].generation;
        g_buffers[i] = {}; g_buffers[i].generation = generation;
    }
    for (uint32_t i = 0; i < DRIVER_MAX_DMA_MAPPINGS; i++) {
        uint32_t generation = g_mappings[i].generation;
        g_mappings[i] = {}; g_mappings[i].generation = generation;
    }
    for (uint32_t i = 0; i < (DMA_ARENA_PAGES + 63u) / 64u; i++)
        g_dma_va[i] = 0;
    g_dma_stats = {};
#ifdef OS64_DRIVER_HOST_TEST
    g_host_next_dma = 0x00200000ULL;
#endif
}

DriverDmaHandle driver_dma_invalid() {
    DriverDmaHandle value = {DRIVER_IDENTITY_INVALID_SLOT, 0};
    return value;
}

int driver_dma_prepare_device(DriverIdentity owner, DriverDeviceIdentity device,
                              uint32_t policy,
                              DriverDmaDomainHandle* out_domain) {
    if (out_domain) *out_domain = {DRIVER_IDENTITY_INVALID_SLOT, 0};
    if (!out_domain || !driver_manager_identity_accepts_resources(owner) ||
        !owner_has_dma(owner) || !resolve_pci(owner, device))
        return DRIVER_LOAD_DMA_DENIED;
    if (policy == DRIVER_DMA_POLICY_REQUIRE_ISOLATION) {
        g_dma_stats.isolation_rejections++;
        return DRIVER_LOAD_DMA_ISOLATION;
    }
    if (policy != DRIVER_DMA_POLICY_TRUSTED_DIRECT)
        return DRIVER_LOAD_DMA_DENIED;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_dma_lock, &token))
        return DRIVER_LOAD_DMA_DENIED;
    DmaDomainRecord* existing = domain_for(owner, device);
    if (existing) {
        out_domain->slot = (uint32_t)(existing - g_domains);
        out_domain->generation = existing->generation;
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_OK;
    }
    uint32_t slot = DRIVER_MAX_DMA_DOMAINS;
    for (uint32_t i = 0; i < DRIVER_MAX_DMA_DOMAINS; i++)
        if (!g_domains[i].active) { slot = i; break; }
    if (slot == DRIVER_MAX_DMA_DOMAINS) {
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_NO_SLOT;
    }
    DmaDomainRecord* domain = &g_domains[slot];
    uint32_t generation = next_generation(domain->generation);
    *domain = {};
    domain->active = 1;
    domain->backend = DRIVER_DMA_BACKEND_DIRECT;
    domain->isolated = 0;
    domain->generation = generation;
    domain->owner = owner;
    domain->device = device;
    g_dma_stats.domains++;
    *out_domain = {slot, generation};
    kernel_spinlock_release(&g_dma_lock, &token);
    return DRIVER_LOAD_OK;
}

int driver_dma_set_mask(DriverIdentity owner, DriverDeviceIdentity device,
                        uint32_t bits) {
    if (bits < 24 || bits > 64) {
        g_dma_stats.mask_rejections++;
        return DRIVER_LOAD_DMA_MASK;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_dma_lock, &token))
        return DRIVER_LOAD_DMA_DENIED;
    DmaDomainRecord* domain = domain_for(owner, device);
    if (!domain || domain->mapping_count != 0 || domain->bus_mastering) {
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_DMA_DENIED;
    }
    domain->mask_bits = bits;
    kernel_spinlock_release(&g_dma_lock, &token);
    return DRIVER_LOAD_OK;
}

int driver_dma_enable_bus_mastering(DriverIdentity owner,
                                    DriverDeviceIdentity device) {
    const PCIDeviceInfo* pci = resolve_pci(owner, device);
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_dma_lock, &token))
        return DRIVER_LOAD_DMA_DENIED;
    DmaDomainRecord* domain = domain_for(owner, device);
    if (!pci || !domain || domain->mask_bits == 0) {
        g_dma_stats.bus_master_rejections++;
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_DMA_DENIED;
    }
    if (!pci_enable_bus_mastering(pci)) {
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_DMA_DENIED;
    }
    domain->bus_mastering = 1;
    kernel_spinlock_release(&g_dma_lock, &token);
    return DRIVER_LOAD_OK;
}

int driver_dma_disable_bus_mastering(DriverIdentity owner,
                                     DriverDeviceIdentity device) {
    const PCIDeviceInfo* pci = resolve_pci(owner, device);
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_dma_lock, &token))
        return DRIVER_LOAD_DMA_DENIED;
    DmaDomainRecord* domain = domain_for(owner, device);
    if (!pci || !domain || !pci_disable_bus_mastering(pci)) {
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_DMA_DENIED;
    }
    domain->bus_mastering = 0;
    kernel_spinlock_release(&g_dma_lock, &token);
    return DRIVER_LOAD_OK;
}

static uint64_t owner_bytes(DriverIdentity owner) {
    uint64_t bytes = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_DMA_BUFFERS; i++)
        if (g_buffers[i].active && driver_identity_equal(g_buffers[i].owner, owner))
            bytes += g_buffers[i].charged_size;
    return bytes;
}

int driver_dma_alloc_coherent(DriverIdentity owner,
                              DriverDeviceIdentity device,
                              uint64_t size, uint64_t alignment,
                              uint64_t boundary, DriverDmaBuffer* out) {
    if (out) *out = {driver_dma_invalid(), 0, {0}, 0, 0, 0};
    if (!out || size == 0 || alignment == 0 || !power_of_two(alignment) ||
        alignment > DRIVER_DMA_OWNER_BUDGET ||
        (boundary != 0 && (!power_of_two(boundary) || boundary < size)))
        return DRIVER_LOAD_BAD_HEADER;
    const uint64_t charged = align_up(size, VM_PAGE_SIZE);
    if (charged < size || charged > DRIVER_DMA_OWNER_BUDGET)
        return DRIVER_LOAD_ALLOCATION_BUDGET;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_dma_lock, &token))
        return DRIVER_LOAD_DMA_DENIED;
    DmaDomainRecord* domain = domain_for(owner, device);
    if (!domain || domain->mask_bits == 0) {
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_DMA_DENIED;
    }
    if (owner_bytes(owner) > DRIVER_DMA_OWNER_BUDGET - charged ||
        g_dma_stats.coherent_bytes > DRIVER_DMA_GLOBAL_BUDGET - charged) {
        g_dma_stats.budget_rejections++;
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_ALLOCATION_BUDGET;
    }
    uint32_t slot = DRIVER_MAX_DMA_BUFFERS;
    for (uint32_t i = 0; i < DRIVER_MAX_DMA_BUFFERS; i++)
        if (!g_buffers[i].active && !g_buffers[i].quarantined) { slot = i; break; }
    const uint32_t pages = (uint32_t)(charged / VM_PAGE_SIZE);
    uint32_t first_page = 0;
    if (slot == DRIVER_MAX_DMA_BUFFERS || !reserve_va(pages, &first_page)) {
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_NO_SLOT;
    }
    uint64_t physical = 0;
    void* cpu = 0;
#ifdef OS64_DRIVER_HOST_TEST
    uint64_t host_alignment = alignment < sizeof(void*) ? sizeof(void*) : alignment;
    uint64_t storage = align_up(charged, host_alignment);
    cpu = aligned_alloc((size_t)host_alignment, (size_t)storage);
    physical = align_up(g_host_next_dma, alignment > VM_PAGE_SIZE ? alignment : VM_PAGE_SIZE);
    if (boundary && physical / boundary != (physical + charged - 1u) / boundary)
        physical = align_up(physical, boundary);
    g_host_next_dma = physical + charged;
#else
    uint32_t alignment_pages = alignment <= VM_PAGE_SIZE ? 1u :
        (uint32_t)(alignment / VM_PAGE_SIZE);
    physical = (uint64_t)(uintptr_t)pmm_alloc_blocks_constrained(
        pages, alignment_pages, boundary, mask_limit(domain->mask_bits));
    cpu = (void*)(uintptr_t)(DRIVER_DMA_ARENA_BASE +
                            (uint64_t)first_page * VM_PAGE_SIZE);
    if (physical != 0) {
        uint32_t mapped = 0;
        for (; mapped < pages; mapped++)
            if (!vm_map_page((uint64_t)(uintptr_t)cpu +
                             (uint64_t)mapped * VM_PAGE_SIZE,
                             physical + (uint64_t)mapped * VM_PAGE_SIZE,
                             VM_FLAG_WRITABLE | VM_FLAG_NO_EXECUTE)) break;
        if (mapped != pages) {
            for (uint32_t i = 0; i < mapped; i++)
                vm_unmap_page((uint64_t)(uintptr_t)cpu +
                              (uint64_t)i * VM_PAGE_SIZE);
            pmm_free_blocks((void*)(uintptr_t)physical, pages);
            physical = 0; cpu = 0;
        }
    }
#endif
    if (!cpu || physical == 0 || physical > mask_limit(domain->mask_bits) ||
        charged - 1u > mask_limit(domain->mask_bits) - physical) {
#ifdef OS64_DRIVER_HOST_TEST
        if (cpu) free(cpu);
#endif
        release_va(first_page, pages);
        g_dma_stats.mask_rejections++;
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_DMA_MASK;
    }
    for (uint64_t i = 0; i < charged; i++) ((uint8_t*)cpu)[i] = 0;
    DmaBufferRecord* buffer = &g_buffers[slot];
    uint32_t generation = next_generation(buffer->generation);
    *buffer = {};
    buffer->generation = generation;
    buffer->owner = owner;
    buffer->device = device;
    buffer->domain_slot = (uint32_t)(domain - g_domains);
    buffer->first_page = first_page;
    buffer->page_count = pages;
    buffer->cpu_address = cpu;
    buffer->physical_address = physical;
    buffer->dma_address = {physical};
    buffer->logical_size = size;
    buffer->charged_size = charged;
    int rr = driver_resource_register(owner, device, DRIVER_RESOURCE_DMA, 0,
                                      slot, size, "dma_coherent",
                                      &buffer->resource);
    if (rr != DRIVER_LOAD_OK) {
#ifdef OS64_DRIVER_HOST_TEST
        free(cpu);
#else
        vm_unmap_range_tlb_safe((uint64_t)(uintptr_t)cpu, pages);
        pmm_free_blocks((void*)(uintptr_t)physical, pages);
#endif
        release_va(first_page, pages);
        *buffer = {}; buffer->generation = generation;
        kernel_spinlock_release(&g_dma_lock, &token);
        return rr;
    }
    buffer->active = 1;
    domain->mapping_count++;
    g_dma_stats.coherent_buffers++;
    g_dma_stats.coherent_bytes += charged;
    g_dma_stats.allocations++;
    if (g_dma_stats.coherent_buffers > g_dma_stats.high_water_buffers)
        g_dma_stats.high_water_buffers = g_dma_stats.coherent_buffers;
    if (g_dma_stats.coherent_bytes > g_dma_stats.peak_bytes)
        g_dma_stats.peak_bytes = g_dma_stats.coherent_bytes;
    out->handle = {slot, generation}; out->cpu_address = cpu;
    out->dma_address = {physical}; out->size = size; out->page_count = pages;
    kernel_spinlock_release(&g_dma_lock, &token);
    return DRIVER_LOAD_OK;
}

static DmaBufferRecord* buffer_for(DriverIdentity owner, DriverDmaHandle handle) {
    if (handle.slot >= DRIVER_MAX_DMA_BUFFERS || handle.generation == 0) return 0;
    DmaBufferRecord* buffer = &g_buffers[handle.slot];
    if (!buffer->active || buffer->generation != handle.generation ||
        !driver_identity_equal(buffer->owner, owner)) return 0;
    return buffer;
}

int driver_dma_free_coherent(DriverIdentity owner, DriverDmaHandle handle) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_dma_lock, &token))
        return DRIVER_LOAD_DMA_DENIED;
    DmaBufferRecord* buffer = buffer_for(owner, handle);
    if (!buffer) {
        if (handle.slot < DRIVER_MAX_DMA_BUFFERS && g_buffers[handle.slot].active &&
            g_buffers[handle.slot].generation == handle.generation)
            g_dma_stats.owner_rejections++;
        else g_dma_stats.stale_rejections++;
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_DMA_DENIED;
    }
    DmaBufferRecord snapshot = *buffer;
    buffer->active = 0;
    buffer->quarantined = 1;
    g_dma_stats.coherent_buffers--;
    kernel_spinlock_release(&g_dma_lock, &token);
#ifdef OS64_DRIVER_HOST_TEST
    free(snapshot.cpu_address);
#else
    if (vm_unmap_range_tlb_safe((uint64_t)(uintptr_t)snapshot.cpu_address,
                                snapshot.page_count) != snapshot.page_count) {
        if (kernel_spinlock_acquire(&g_dma_lock, &token)) {
            g_dma_stats.quarantined_buffers++;
            kernel_spinlock_release(&g_dma_lock, &token);
        }
        return DRIVER_LOAD_RESOURCE_DENIED;
    }
    pmm_free_blocks((void*)(uintptr_t)snapshot.physical_address,
                    snapshot.page_count);
#endif
    driver_resource_release(owner, snapshot.resource, DRIVER_RESOURCE_DMA);
    if (!kernel_spinlock_acquire(&g_dma_lock, &token))
        return DRIVER_LOAD_RESOURCE_DENIED;
    release_va(snapshot.first_page, snapshot.page_count);
    DmaDomainRecord* domain = &g_domains[snapshot.domain_slot];
    if (domain->mapping_count) domain->mapping_count--;
    g_dma_stats.coherent_bytes -= snapshot.charged_size;
    g_dma_stats.releases++;
    uint32_t generation = buffer->generation;
    *buffer = {}; buffer->generation = generation;
    kernel_spinlock_release(&g_dma_lock, &token);
    return DRIVER_LOAD_OK;
}

static int valid_direction(uint32_t direction) {
    return direction == DRIVER_DMA_TO_DEVICE ||
           direction == DRIVER_DMA_FROM_DEVICE ||
           direction == DRIVER_DMA_BIDIRECTIONAL;
}

static int append_range_segments(DriverAllocationHandle allocation,
                                 const DriverPinnedAllocation* pinned,
                                 uint64_t mask, DriverDmaSegment* segments,
                                 uint32_t* segment_count) {
    uint64_t remaining = pinned->size;
    uintptr_t cursor = (uintptr_t)pinned->address;
    while (remaining != 0) {
        const uint64_t page_offset = cursor & (VM_PAGE_SIZE - 1u);
        uint64_t chunk = VM_PAGE_SIZE - page_offset;
        if (chunk > remaining) chunk = remaining;
        uint64_t physical;
#ifdef OS64_DRIVER_HOST_TEST
        const uint64_t page = ((uint64_t)cursor / VM_PAGE_SIZE) & 0xFFFFu;
        physical = 0x02000000ULL +
            (uint64_t)allocation.slot * 0x40000ULL + page * 0x2000ULL +
            page_offset;
#else
        physical = vm_get_phys((uint64_t)cursor);
#endif
        if (physical == 0 || physical > mask || chunk - 1u > mask - physical)
            return DRIVER_LOAD_DMA_MASK;
        if (*segment_count != 0) {
            DriverDmaSegment* previous = &segments[*segment_count - 1u];
            if (previous->address.value + previous->length == physical) {
                previous->length += chunk;
                cursor += chunk; remaining -= chunk;
                continue;
            }
        }
        if (*segment_count >= DRIVER_DMA_MAX_SEGMENTS)
            return DRIVER_LOAD_NO_SLOT;
        segments[*segment_count].address = {physical};
        segments[*segment_count].length = chunk;
        (*segment_count)++;
        cursor += chunk;
        remaining -= chunk;
    }
    return DRIVER_LOAD_OK;
}

int driver_dma_map_sg(DriverIdentity owner, DriverDeviceIdentity device,
                      const DriverDmaSource* sources, uint32_t source_count,
                      uint32_t direction, DriverDmaMapping* out) {
    if (out) *out = {};
    if (!out || !sources || source_count == 0 ||
        source_count > DRIVER_DMA_MAX_SOURCES || !valid_direction(direction))
        return DRIVER_LOAD_BAD_HEADER;
    uint64_t total = 0;
    for (uint32_t i = 0; i < source_count; i++) {
        if (sources[i].length == 0 || total > UINT64_MAX - sources[i].length)
            return DRIVER_LOAD_BAD_HEADER;
        total += sources[i].length;
    }
    if (total > DRIVER_DMA_OWNER_BUDGET)
        return DRIVER_LOAD_ALLOCATION_BUDGET;
    DmaDomainRecord domain_snapshot;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_dma_lock, &token))
        return DRIVER_LOAD_DMA_DENIED;
    DmaDomainRecord* domain = domain_for(owner, device);
    if (!domain || domain->mask_bits == 0) {
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_DMA_DENIED;
    }
    domain_snapshot = *domain;
    kernel_spinlock_release(&g_dma_lock, &token);

    DriverPinnedAllocation pinned[DRIVER_DMA_MAX_SOURCES] = {};
    DriverDmaSegment segments[DRIVER_DMA_MAX_SEGMENTS] = {};
    uint32_t pinned_count = 0;
    uint32_t segment_count = 0;
    int result = DRIVER_LOAD_OK;
    int need_bounce = 0;
    for (; pinned_count < source_count; pinned_count++) {
        const DriverDmaSource* source = &sources[pinned_count];
        result = driver_allocation_pin(owner, source->allocation,
                                       source->offset, source->length,
                                       &pinned[pinned_count]);
        if (result != DRIVER_LOAD_OK) break;
        if (!need_bounce) {
            result = append_range_segments(source->allocation,
                                           &pinned[pinned_count],
                                           mask_limit(domain_snapshot.mask_bits),
                                           segments, &segment_count);
            if (result == DRIVER_LOAD_DMA_MASK) {
                need_bounce = 1;
                result = DRIVER_LOAD_OK;
                segment_count = 0;
            } else if (result != DRIVER_LOAD_OK) {
                pinned_count++;
                break;
            }
        }
    }
    DriverDmaBuffer bounce = {};
    if (result == DRIVER_LOAD_OK && need_bounce) {
        result = driver_dma_alloc_coherent(owner, device, total, VM_PAGE_SIZE,
                                           0, &bounce);
        if (result == DRIVER_LOAD_OK) {
            segments[0].address = bounce.dma_address;
            segments[0].length = total;
            segment_count = 1;
            if (direction != DRIVER_DMA_FROM_DEVICE) {
                uint64_t copied = 0;
                for (uint32_t source = 0; source < source_count; source++) {
                    for (uint64_t byte = 0; byte < pinned[source].size; byte++)
                        ((uint8_t*)bounce.cpu_address)[copied + byte] =
                            ((uint8_t*)pinned[source].address)[byte];
                    copied += pinned[source].size;
                }
            }
        }
    }
    if (result != DRIVER_LOAD_OK) {
        while (pinned_count != 0) {
            pinned_count--;
            driver_allocation_unpin(owner, sources[pinned_count].allocation);
        }
        if (result == DRIVER_LOAD_NO_SLOT) g_dma_stats.segment_overflow++;
        else if (result == DRIVER_LOAD_DMA_MASK) g_dma_stats.mask_rejections++;
        return result;
    }

    if (!kernel_spinlock_acquire(&g_dma_lock, &token)) {
        if (bounce.handle.generation)
            driver_dma_free_coherent(owner, bounce.handle);
        for (uint32_t i = 0; i < source_count; i++)
            driver_allocation_unpin(owner, sources[i].allocation);
        return DRIVER_LOAD_DMA_DENIED;
    }
    domain = domain_for(owner, device);
    uint32_t slot = DRIVER_MAX_DMA_MAPPINGS;
    for (uint32_t i = 0; i < DRIVER_MAX_DMA_MAPPINGS; i++)
        if (!g_mappings[i].active) { slot = i; break; }
    if (!domain || slot == DRIVER_MAX_DMA_MAPPINGS) {
        kernel_spinlock_release(&g_dma_lock, &token);
        if (bounce.handle.generation)
            driver_dma_free_coherent(owner, bounce.handle);
        for (uint32_t i = 0; i < source_count; i++)
            driver_allocation_unpin(owner, sources[i].allocation);
        return DRIVER_LOAD_NO_SLOT;
    }
    DmaMappingRecord* mapping = &g_mappings[slot];
    uint32_t generation = next_generation(mapping->generation);
    *mapping = {};
    mapping->generation = generation;
    mapping->owner = owner; mapping->device = device;
    mapping->direction = (uint8_t)direction;
    mapping->sync_state = 1;
    mapping->source_count = (uint8_t)source_count;
    mapping->domain_slot = (uint32_t)(domain - g_domains);
    mapping->segment_count = segment_count;
    for (uint32_t i = 0; i < source_count; i++) {
        mapping->sources[i] = sources[i].allocation;
        mapping->source_addresses[i] = pinned[i].address;
        mapping->source_lengths[i] = pinned[i].size;
    }
    for (uint32_t i = 0; i < segment_count; i++)
        mapping->segments[i] = segments[i];
    mapping->bounce = bounce.handle;
    mapping->bounce_cpu = bounce.cpu_address;
    mapping->bounce_size = bounce.size;
    int rr = driver_resource_register(owner, device, DRIVER_RESOURCE_DMA,
                                      direction, slot, total,
                                      "dma_stream", &mapping->resource);
    if (rr != DRIVER_LOAD_OK) {
        *mapping = {}; mapping->generation = generation;
        kernel_spinlock_release(&g_dma_lock, &token);
        if (bounce.handle.generation)
            driver_dma_free_coherent(owner, bounce.handle);
        for (uint32_t i = 0; i < source_count; i++)
            driver_allocation_unpin(owner, sources[i].allocation);
        return rr;
    }
    mapping->active = 1;
    domain->mapping_count++;
    g_dma_stats.streaming_mappings++;
    g_dma_stats.pinned_sources += source_count;
    if (bounce.handle.generation) g_dma_stats.bounce_mappings++;
    g_dma_stats.streaming_maps++;
    out->handle = {slot, generation};
    out->segment_count = segment_count; out->direction = direction;
    for (uint32_t i = 0; i < segment_count; i++) out->segments[i] = segments[i];
    kernel_spinlock_release(&g_dma_lock, &token);
    return DRIVER_LOAD_OK;
}

int driver_dma_map_buffer(DriverIdentity owner, DriverDeviceIdentity device,
                          DriverAllocationHandle allocation, uint64_t offset,
                          uint64_t length, uint32_t direction,
                          DriverDmaMapping* out) {
    DriverDmaSource source = {allocation, offset, length};
    return driver_dma_map_sg(owner, device, &source, 1, direction, out);
}

static DmaMappingRecord* mapping_for(DriverIdentity owner,
                                     DriverDmaMappingHandle handle) {
    if (handle.slot >= DRIVER_MAX_DMA_MAPPINGS || handle.generation == 0) return 0;
    DmaMappingRecord* mapping = &g_mappings[handle.slot];
    if (!mapping->active || mapping->generation != handle.generation ||
        !driver_identity_equal(mapping->owner, owner)) return 0;
    return mapping;
}

int driver_dma_sync_for_cpu(DriverIdentity owner,
                            DriverDmaMappingHandle handle) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_dma_lock, &token)) return DRIVER_LOAD_DMA_DENIED;
    DmaMappingRecord* mapping = mapping_for(owner, handle);
    if (!mapping || mapping->direction == DRIVER_DMA_TO_DEVICE ||
        mapping->sync_state != 1) {
        g_dma_stats.sync_rejections++;
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_DMA_SYNC;
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (mapping->bounce.generation != 0) {
        uint64_t copied = 0;
        for (uint32_t source = 0; source < mapping->source_count; source++) {
            for (uint64_t byte = 0; byte < mapping->source_lengths[source]; byte++)
                ((uint8_t*)mapping->source_addresses[source])[byte] =
                    ((uint8_t*)mapping->bounce_cpu)[copied + byte];
            copied += mapping->source_lengths[source];
        }
    }
    mapping->sync_state = 2;
    g_dma_stats.sync_for_cpu++;
    kernel_spinlock_release(&g_dma_lock, &token);
    return DRIVER_LOAD_OK;
}

int driver_dma_sync_for_device(DriverIdentity owner,
                               DriverDmaMappingHandle handle) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_dma_lock, &token)) return DRIVER_LOAD_DMA_DENIED;
    DmaMappingRecord* mapping = mapping_for(owner, handle);
    if (!mapping || mapping->sync_state != 2) {
        g_dma_stats.sync_rejections++;
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_DMA_SYNC;
    }
    __atomic_thread_fence(__ATOMIC_RELEASE);
    if (mapping->bounce.generation != 0 &&
        mapping->direction != DRIVER_DMA_FROM_DEVICE) {
        uint64_t copied = 0;
        for (uint32_t source = 0; source < mapping->source_count; source++) {
            for (uint64_t byte = 0; byte < mapping->source_lengths[source]; byte++)
                ((uint8_t*)mapping->bounce_cpu)[copied + byte] =
                    ((uint8_t*)mapping->source_addresses[source])[byte];
            copied += mapping->source_lengths[source];
        }
    }
    mapping->sync_state = 1;
    g_dma_stats.sync_for_device++;
    kernel_spinlock_release(&g_dma_lock, &token);
    return DRIVER_LOAD_OK;
}

int driver_dma_unmap(DriverIdentity owner, DriverDmaMappingHandle handle) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_dma_lock, &token)) return DRIVER_LOAD_DMA_DENIED;
    DmaMappingRecord* mapping = mapping_for(owner, handle);
    if (!mapping) {
        g_dma_stats.stale_rejections++;
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_DMA_DENIED;
    }
    if (mapping->direction != DRIVER_DMA_TO_DEVICE && mapping->sync_state != 2) {
        g_dma_stats.sync_rejections++;
        kernel_spinlock_release(&g_dma_lock, &token);
        return DRIVER_LOAD_DMA_SYNC;
    }
    DmaMappingRecord snapshot = *mapping;
    mapping->active = 0;
    DmaDomainRecord* domain = &g_domains[snapshot.domain_slot];
    if (domain->mapping_count) domain->mapping_count--;
    g_dma_stats.streaming_mappings--;
    g_dma_stats.pinned_sources -= snapshot.source_count;
    if (snapshot.bounce.generation != 0) g_dma_stats.bounce_mappings--;
    g_dma_stats.streaming_unmaps++;
    uint32_t generation = mapping->generation;
    *mapping = {}; mapping->generation = generation;
    kernel_spinlock_release(&g_dma_lock, &token);
    driver_resource_release(owner, snapshot.resource, DRIVER_RESOURCE_DMA);
    for (uint32_t i = 0; i < snapshot.source_count; i++)
        driver_allocation_unpin(owner, snapshot.sources[i]);
    if (snapshot.bounce.generation != 0)
        driver_dma_free_coherent(owner, snapshot.bounce);
    return DRIVER_LOAD_OK;
}

static int current_owner(DriverIdentity* out) {
    DriverExecutionContext context;
    if (!driver_execution_current(&context) ||
        context.kind != DRIVER_CONTEXT_THREAD_SLEEPABLE) return 0;
    *out = context.owner; return 1;
}
int driver_dma_prepare_device_current(DriverDeviceIdentity device,
                                      uint32_t policy,
                                      DriverDmaDomainHandle* out_domain) {
    DriverIdentity owner; return current_owner(&owner) ?
        driver_dma_prepare_device(owner, device, policy, out_domain) :
        DRIVER_LOAD_CONTEXT_DENIED;
}
int driver_dma_set_mask_current(DriverDeviceIdentity device, uint32_t bits) {
    DriverIdentity owner; return current_owner(&owner) ?
        driver_dma_set_mask(owner, device, bits) : DRIVER_LOAD_CONTEXT_DENIED;
}
int driver_dma_enable_bus_mastering_current(DriverDeviceIdentity device) {
    DriverIdentity owner; return current_owner(&owner) ?
        driver_dma_enable_bus_mastering(owner, device) :
        DRIVER_LOAD_CONTEXT_DENIED;
}
int driver_dma_disable_bus_mastering_current(DriverDeviceIdentity device) {
    DriverIdentity owner; return current_owner(&owner) ?
        driver_dma_disable_bus_mastering(owner, device) :
        DRIVER_LOAD_CONTEXT_DENIED;
}
int driver_dma_alloc_coherent_current(DriverDeviceIdentity device,
                                      uint64_t size, uint64_t alignment,
                                      uint64_t boundary,
                                      DriverDmaBuffer* out) {
    DriverIdentity owner; return current_owner(&owner) ?
        driver_dma_alloc_coherent(owner, device, size, alignment, boundary, out) :
        DRIVER_LOAD_CONTEXT_DENIED;
}
int driver_dma_free_coherent_current(DriverDmaHandle handle) {
    DriverIdentity owner; return current_owner(&owner) ?
        driver_dma_free_coherent(owner, handle) : DRIVER_LOAD_CONTEXT_DENIED;
}
int driver_dma_map_buffer_current(DriverDeviceIdentity device,
                                  DriverAllocationHandle allocation,
                                  uint64_t offset, uint64_t length,
                                  uint32_t direction, DriverDmaMapping* out) {
    DriverIdentity owner; return current_owner(&owner) ?
        driver_dma_map_buffer(owner, device, allocation, offset, length,
                              direction, out) : DRIVER_LOAD_CONTEXT_DENIED;
}
int driver_dma_map_sg_current(DriverDeviceIdentity device,
                              const DriverDmaSource* sources,
                              uint32_t source_count, uint32_t direction,
                              DriverDmaMapping* out) {
    DriverIdentity owner; return current_owner(&owner) ?
        driver_dma_map_sg(owner, device, sources, source_count, direction, out) :
        DRIVER_LOAD_CONTEXT_DENIED;
}
int driver_dma_sync_for_cpu_current(DriverDmaMappingHandle handle) {
    DriverIdentity owner; return current_owner(&owner) ?
        driver_dma_sync_for_cpu(owner, handle) : DRIVER_LOAD_CONTEXT_DENIED;
}
int driver_dma_sync_for_device_current(DriverDmaMappingHandle handle) {
    DriverIdentity owner; return current_owner(&owner) ?
        driver_dma_sync_for_device(owner, handle) : DRIVER_LOAD_CONTEXT_DENIED;
}
int driver_dma_unmap_current(DriverDmaMappingHandle handle) {
    DriverIdentity owner; return current_owner(&owner) ?
        driver_dma_unmap(owner, handle) : DRIVER_LOAD_CONTEXT_DENIED;
}

uint32_t driver_dma_release_owner(DriverIdentity owner) {
    uint32_t released = 0;
    for (;;) {
        DriverDmaMappingHandle handle = {DRIVER_IDENTITY_INVALID_SLOT, 0};
        uint32_t direction = 0;
        KernelSpinlockToken token;
        if (!kernel_spinlock_acquire(&g_dma_lock, &token)) break;
        for (uint32_t i = 0; i < DRIVER_MAX_DMA_MAPPINGS; i++) {
            if (g_mappings[i].active &&
                driver_identity_equal(g_mappings[i].owner, owner)) {
                handle = {i, g_mappings[i].generation};
                direction = g_mappings[i].direction;
                break;
            }
        }
        kernel_spinlock_release(&g_dma_lock, &token);
        if (handle.slot == DRIVER_IDENTITY_INVALID_SLOT) break;
        if (direction != DRIVER_DMA_TO_DEVICE)
            driver_dma_sync_for_cpu(owner, handle);
        if (driver_dma_unmap(owner, handle) != DRIVER_LOAD_OK) break;
        released++;
    }
    for (;;) {
        DriverDmaHandle handle = driver_dma_invalid();
        KernelSpinlockToken token;
        if (!kernel_spinlock_acquire(&g_dma_lock, &token)) break;
        for (uint32_t i = 0; i < DRIVER_MAX_DMA_BUFFERS; i++)
            if (g_buffers[i].active && driver_identity_equal(g_buffers[i].owner, owner)) {
                handle = {i, g_buffers[i].generation}; break;
            }
        kernel_spinlock_release(&g_dma_lock, &token);
        if (handle.slot == DRIVER_IDENTITY_INVALID_SLOT) break;
        if (driver_dma_free_coherent(owner, handle) != DRIVER_LOAD_OK) break;
        released++;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_dma_lock, &token)) return released;
    for (uint32_t i = 0; i < DRIVER_MAX_DMA_DOMAINS; i++) {
        DmaDomainRecord* domain = &g_domains[i];
        if (!domain->active || !driver_identity_equal(domain->owner, owner)) continue;
        const PCIDeviceInfo* pci = resolve_pci(owner, domain->device);
        if (domain->bus_mastering && pci) pci_disable_bus_mastering(pci);
        if (domain->mapping_count == 0) {
            uint32_t generation = domain->generation;
            *domain = {}; domain->generation = generation;
            g_dma_stats.domains--;
        }
    }
    kernel_spinlock_release(&g_dma_lock, &token);
    return released;
}

uint32_t driver_dma_owner_count(DriverIdentity owner) {
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_dma_lock, &token)) return 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < DRIVER_MAX_DMA_BUFFERS; i++)
        if ((g_buffers[i].active || g_buffers[i].quarantined) &&
            driver_identity_equal(g_buffers[i].owner, owner)) count++;
    for (uint32_t i = 0; i < DRIVER_MAX_DMA_DOMAINS; i++)
        if (g_domains[i].active && driver_identity_equal(g_domains[i].owner, owner)) count++;
    kernel_spinlock_release(&g_dma_lock, &token);
    return count;
}

void driver_dma_get_stats(DriverDmaStats* out) {
    if (!out) return;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&g_dma_lock, &token)) { *out = {}; return; }
    *out = g_dma_stats;
    kernel_spinlock_release(&g_dma_lock, &token);
}
