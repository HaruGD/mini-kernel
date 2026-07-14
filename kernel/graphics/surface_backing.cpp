#include "kernel/graphics/surface_backing.h"

#include <stddef.h>

#include "kernel/mm/pmm.h"

struct SurfaceBackingSlot {
    uint8_t active;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t page_count;
    uint32_t byte_size;
    uint32_t reserved2;
    uint64_t physical_pages[KERNEL_GRAPHICS_SURFACE_MAX_PAGES];
};

static SurfaceBackingSlot backing_slots[KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS];
static KernelGraphicsSurfaceBackingStats backing_stats;

static const uint64_t SURFACE_SLOT_SIZE =
    (uint64_t)KERNEL_GRAPHICS_SURFACE_MAX_PAGES * VM_PAGE_SIZE;

static_assert(VM_KERNEL_SURFACE_BASE +
                  (uint64_t)KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS * SURFACE_SLOT_SIZE <=
              VM_KERNEL_SURFACE_LIMIT,
              "kernel surface virtual arena is too small");

static uint64_t slot_virtual_base(uint32_t slot) {
    return VM_KERNEL_SURFACE_BASE + (uint64_t)slot * SURFACE_SLOT_SIZE;
}

static void clear_page(uint64_t phys) {
    uint8_t* bytes = (uint8_t*)(uintptr_t)phys;
    for (uint32_t i = 0; i < (uint32_t)VM_PAGE_SIZE; i++) {
        bytes[i] = 0;
    }
}

static void clear_slot_metadata(SurfaceBackingSlot* slot) {
    if (slot == 0) {
        return;
    }
    slot->active = 0;
    slot->page_count = 0;
    slot->byte_size = 0;
}

static void rollback_allocation(uint32_t slot_index, uint32_t page_count) {
    SurfaceBackingSlot* slot = &backing_slots[slot_index];
    uint64_t virtual_base = slot_virtual_base(slot_index);
    uint32_t remaining = 0;
    for (uint32_t page = 0; page < page_count; page++) {
        uint64_t virt = virtual_base + (uint64_t)page * VM_PAGE_SIZE;
        uint64_t phys = slot->physical_pages[page];
        if (vm_unmap_page(virt)) {
            pmm_free_block((void*)(uintptr_t)phys);
            backing_stats.rollback_pages++;
        } else {
            backing_stats.unmap_failures++;
            remaining++;
        }
        if (vm_get_phys(virt) == 0) {
            slot->physical_pages[page] = 0;
        }
    }
    if (remaining != 0) {
        slot->active = 1;
        slot->page_count = page_count;
        slot->byte_size = page_count * (uint32_t)VM_PAGE_SIZE;
        backing_stats.active_backings++;
        backing_stats.mapped_pages += remaining;
        backing_stats.mapped_bytes += (uint64_t)remaining * VM_PAGE_SIZE;
        return;
    }
    clear_slot_metadata(slot);
}

void kernel_graphics_surface_backing_init() {
    for (uint32_t slot = 0; slot < KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS; slot++) {
        if (backing_slots[slot].active) {
            kernel_graphics_surface_backing_release(slot);
        }
        for (uint32_t page = 0; page < KERNEL_GRAPHICS_SURFACE_MAX_PAGES; page++) {
            backing_slots[slot].physical_pages[page] = 0;
        }
        clear_slot_metadata(&backing_slots[slot]);
    }

    backing_stats.active_backings = 0;
    backing_stats.mapped_pages = 0;
    backing_stats.mapped_bytes = 0;
    backing_stats.allocation_attempts = 0;
    backing_stats.allocation_failures = 0;
    backing_stats.rollback_pages = 0;
    backing_stats.release_pages = 0;
    backing_stats.unmap_failures = 0;
}

int kernel_graphics_surface_backing_allocate(uint32_t slot_index,
                                             uint32_t byte_size,
                                             uint32_t** pixels_out,
                                             uint32_t* page_count_out) {
    if (pixels_out != 0) {
        *pixels_out = 0;
    }
    if (page_count_out != 0) {
        *page_count_out = 0;
    }
    backing_stats.allocation_attempts++;

    const uint32_t maximum_byte_size =
        KERNEL_GRAPHICS_SURFACE_MAX_PIXELS * KERNEL_GRAPHICS_SURFACE_BYTES_PER_PIXEL;
    if (slot_index >= KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS || byte_size == 0 ||
        byte_size > maximum_byte_size || pixels_out == 0 || page_count_out == 0) {
        backing_stats.allocation_failures++;
        return 0;
    }

    uint32_t page_count =
        (byte_size + (uint32_t)VM_PAGE_SIZE - 1u) / (uint32_t)VM_PAGE_SIZE;
    SurfaceBackingSlot* slot = &backing_slots[slot_index];
    if (page_count == 0 || page_count > KERNEL_GRAPHICS_SURFACE_MAX_PAGES ||
        slot->active || backing_stats.mapped_pages > KERNEL_GRAPHICS_SURFACE_MAX_TOTAL_PAGES ||
        page_count > KERNEL_GRAPHICS_SURFACE_MAX_TOTAL_PAGES - backing_stats.mapped_pages) {
        backing_stats.allocation_failures++;
        return 0;
    }

    uint64_t virtual_base = slot_virtual_base(slot_index);
    uint32_t mapped = 0;
    for (uint32_t page = 0; page < page_count; page++) {
        uint64_t phys = (uint64_t)(uintptr_t)pmm_alloc_block();
        if (phys == 0) {
            rollback_allocation(slot_index, mapped);
            backing_stats.allocation_failures++;
            return 0;
        }

        clear_page(phys);
        uint64_t virt = virtual_base + (uint64_t)page * VM_PAGE_SIZE;
        if (!vm_map_page(virt,
                         phys,
                         VM_FLAG_PRESENT | VM_FLAG_WRITABLE |
                             VM_FLAG_GLOBAL | VM_FLAG_NO_EXECUTE)) {
            pmm_free_block((void*)(uintptr_t)phys);
            rollback_allocation(slot_index, mapped);
            backing_stats.allocation_failures++;
            return 0;
        }
        slot->physical_pages[page] = phys;
        mapped++;
    }

    slot->active = 1;
    slot->page_count = page_count;
    slot->byte_size = byte_size;
    backing_stats.active_backings++;
    backing_stats.mapped_pages += page_count;
    backing_stats.mapped_bytes += (uint64_t)page_count * VM_PAGE_SIZE;

    if (pixels_out != 0) {
        *pixels_out = (uint32_t*)(uintptr_t)virtual_base;
    }
    if (page_count_out != 0) {
        *page_count_out = page_count;
    }
    return 1;
}

void kernel_graphics_surface_backing_release(uint32_t slot_index) {
    if (slot_index >= KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS) {
        return;
    }

    SurfaceBackingSlot* slot = &backing_slots[slot_index];
    if (!slot->active) {
        return;
    }

    uint32_t page_count = slot->page_count;
    uint64_t virtual_base = slot_virtual_base(slot_index);
    uint32_t released = 0;
    uint32_t remaining = 0;
    for (uint32_t page = 0; page < page_count; page++) {
        uint64_t virt = virtual_base + (uint64_t)page * VM_PAGE_SIZE;
        uint64_t phys = slot->physical_pages[page];
        if (phys == 0) {
            continue;
        }
        if (vm_unmap_page(virt)) {
            pmm_free_block((void*)(uintptr_t)phys);
            backing_stats.release_pages++;
            released++;
            slot->physical_pages[page] = 0;
        } else {
            backing_stats.unmap_failures++;
            remaining++;
        }
    }

    if (backing_stats.mapped_pages >= released) {
        backing_stats.mapped_pages -= released;
    } else {
        backing_stats.mapped_pages = 0;
    }
    uint64_t mapped_bytes = (uint64_t)released * VM_PAGE_SIZE;
    if (backing_stats.mapped_bytes >= mapped_bytes) {
        backing_stats.mapped_bytes -= mapped_bytes;
    } else {
        backing_stats.mapped_bytes = 0;
    }
    if (remaining != 0) {
        return;
    }
    if (backing_stats.active_backings != 0) {
        backing_stats.active_backings--;
    }
    clear_slot_metadata(slot);
}

int kernel_graphics_surface_backing_get_info(uint32_t slot_index,
                                             KernelGraphicsSurfaceBackingInfo* info) {
    if (slot_index >= KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS || info == 0 ||
        !backing_slots[slot_index].active) {
        return 0;
    }
    const SurfaceBackingSlot* slot = &backing_slots[slot_index];
    info->active = 1;
    info->page_count = slot->page_count;
    info->byte_size = slot->byte_size;
    info->reserved = 0;
    info->virtual_base = slot_virtual_base(slot_index);
    return 1;
}

uint64_t kernel_graphics_surface_backing_get_phys(uint32_t slot_index, uint32_t page) {
    if (slot_index >= KERNEL_GRAPHICS_SURFACE_MAX_OBJECTS ||
        !backing_slots[slot_index].active || page >= backing_slots[slot_index].page_count) {
        return 0;
    }
    return backing_slots[slot_index].physical_pages[page];
}

void kernel_graphics_surface_backing_get_stats(KernelGraphicsSurfaceBackingStats* stats) {
    if (stats != 0) {
        *stats = backing_stats;
    }
}
