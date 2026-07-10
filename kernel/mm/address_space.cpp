#include "kernel/mm/address_space.h"

#include "kernel/mm/pmm.h"

static uint64_t align_down_page(uint64_t value) {
    return value & ~(VM_PAGE_SIZE - 1ULL);
}

static uint64_t align_up_page(uint64_t value) {
    return (value + VM_PAGE_SIZE - 1ULL) & ~(VM_PAGE_SIZE - 1ULL);
}

static uint32_t flags_to_rights(uint64_t flags) {
    uint32_t rights = ADDRESS_SPACE_REGION_READ;
    if (flags & VM_FLAG_WRITABLE) {
        rights |= ADDRESS_SPACE_REGION_WRITE;
    }
    if (!(flags & VM_FLAG_NO_EXECUTE)) {
        rights |= ADDRESS_SPACE_REGION_EXECUTE;
    }
    return rights;
}

void address_space_init(AddressSpace* space) {
    if (space == 0) {
        return;
    }
    space->root_phys = 0;
    address_space_reset_user(space);
}

int address_space_ensure_root(AddressSpace* space) {
    if (space == 0) {
        return 0;
    }
    if (space->root_phys != 0) {
        return 1;
    }
    space->root_phys = vm_create_root();
    return space->root_phys != 0;
}

void address_space_reset_user(AddressSpace* space) {
    if (space == 0) {
        return;
    }
    space->code_base = 0;
    space->elf_link_base = 0;
    space->stack_guard_base = 0;
    space->stack_base = 0;
    space->heap_base = 0;
    space->heap_break = 0;
    space->heap_mapped_end = 0;
    space->heap_limit = 0;
    space->code_page_count = 0;
    space->elf_alias_page_count = 0;
    space->stack_guard_page_count = 0;
    space->stack_page_count = 0;
    space->heap_page_count = 0;
    space->region_count = 0;
    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX_REGIONS; i++) {
        space->regions[i].active = 0;
        space->regions[i].rights = 0;
        space->regions[i].start = 0;
        space->regions[i].end = 0;
    }
}

void address_space_activate(const AddressSpace* space) {
    if (space == 0 || space->root_phys == 0) {
        address_space_activate_kernel();
        return;
    }
    vm_switch_root(space->root_phys);
}

void address_space_activate_kernel() {
    vm_switch_root(vm_get_root_phys());
}

int address_space_add_region(AddressSpace* space, uint64_t start, uint64_t size, uint32_t rights) {
    if (space == 0 || size == 0) {
        return 1;
    }

    uint64_t begin = align_down_page(start);
    uint64_t end = align_up_page(start + size);
    if (end <= begin) {
        return 0;
    }

    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX_REGIONS; i++) {
        AddressSpaceRegion* region = &space->regions[i];
        if (!region->active) {
            continue;
        }
        if (region->end == begin && region->rights == rights) {
            region->end = end;
            return 1;
        }
        if (region->start == end && region->rights == rights) {
            region->start = begin;
            return 1;
        }
    }

    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX_REGIONS; i++) {
        AddressSpaceRegion* region = &space->regions[i];
        if (region->active) {
            continue;
        }
        region->active = 1;
        region->rights = rights;
        region->start = begin;
        region->end = end;
        space->region_count++;
        return 1;
    }
    return 0;
}

void address_space_remove_region(AddressSpace* space, uint64_t start, uint64_t size) {
    if (space == 0 || size == 0) {
        return;
    }

    uint64_t begin = align_down_page(start);
    uint64_t end = align_up_page(start + size);
    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX_REGIONS; i++) {
        AddressSpaceRegion* region = &space->regions[i];
        if (!region->active || begin >= region->end || end <= region->start) {
            continue;
        }
        if (begin <= region->start && end >= region->end) {
            region->active = 0;
            region->rights = 0;
            region->start = 0;
            region->end = 0;
            if (space->region_count > 0) {
                space->region_count--;
            }
            continue;
        }
        if (begin <= region->start) {
            region->start = end;
            continue;
        }
        if (end >= region->end) {
            region->end = begin;
            continue;
        }

        for (uint32_t j = 0; j < ADDRESS_SPACE_MAX_REGIONS; j++) {
            AddressSpaceRegion* split = &space->regions[j];
            if (split->active) {
                continue;
            }
            split->active = 1;
            split->rights = region->rights;
            split->start = end;
            split->end = region->end;
            region->end = begin;
            space->region_count++;
            return;
        }
        region->end = begin;
        return;
    }
}

static const AddressSpaceRegion* find_region(const AddressSpace* space, uint64_t address) {
    if (space == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX_REGIONS; i++) {
        const AddressSpaceRegion* region = &space->regions[i];
        if (region->active && address >= region->start && address < region->end) {
            return region;
        }
    }
    return 0;
}

int address_space_owns_address(const AddressSpace* space, uint64_t address) {
    return find_region(space, address) != 0;
}

int address_space_buffer_accessible(const AddressSpace* space, uint64_t start, uint32_t size, int writable) {
    if (size == 0) {
        return 1;
    }
    if (space == 0 || start == 0) {
        return 0;
    }

    uint64_t end = start + size - 1;
    if (end < start) {
        return 0;
    }

    uint64_t address = start;
    while (1) {
        const AddressSpaceRegion* region = find_region(space, address);
        if (region == 0 || (writable && !(region->rights & ADDRESS_SPACE_REGION_WRITE))) {
            return 0;
        }
        uint64_t flags = address_space_get_flags(space, address);
        if (!(flags & VM_FLAG_USER) ||
            (writable && !(flags & VM_FLAG_WRITABLE))) {
            return 0;
        }
        if ((address & ~(VM_PAGE_SIZE - 1ULL)) == (end & ~(VM_PAGE_SIZE - 1ULL))) {
            break;
        }
        address = (address & ~(VM_PAGE_SIZE - 1ULL)) + VM_PAGE_SIZE;
    }
    return address_space_owns_address(space, end);
}

int address_space_map_page(AddressSpace* space, uint64_t virt, uint64_t phys, uint64_t flags) {
    if (!address_space_ensure_root(space)) {
        return 0;
    }
    return vm_map_page_in_root(space->root_phys, virt, phys, flags);
}

int address_space_unmap_page(AddressSpace* space, uint64_t virt) {
    if (space == 0 || space->root_phys == 0) {
        return 0;
    }
    return vm_unmap_page_in_root(space->root_phys, virt);
}

int address_space_protect_range(AddressSpace* space, uint64_t virt, uint64_t size, uint64_t flags) {
    if (!address_space_ensure_root(space)) {
        return 0;
    }
    return vm_protect_range_in_root(space->root_phys, virt, size, flags);
}

int address_space_alloc_map_range(AddressSpace* space,
                                  uint64_t virt,
                                  uint64_t size,
                                  uint64_t flags,
                                  uint32_t* out_page_count) {
    if (!address_space_ensure_root(space)) {
        return 0;
    }
    if (!vm_alloc_map_range_in_root(space->root_phys, virt, size, flags, out_page_count)) {
        return 0;
    }
    return address_space_add_region(space, virt, size, flags_to_rights(flags));
}

uint32_t address_space_unmap_free_range(AddressSpace* space, uint64_t virt, uint32_t page_count) {
    if (space == 0 || space->root_phys == 0) {
        return 0;
    }
    uint32_t unmapped = vm_unmap_free_range_in_root(space->root_phys, virt, page_count);
    if (unmapped != 0) {
        address_space_remove_region(space, virt, (uint64_t)page_count * VM_PAGE_SIZE);
    }
    return unmapped;
}

uint64_t address_space_get_phys(const AddressSpace* space, uint64_t virt) {
    if (space == 0 || space->root_phys == 0) {
        return 0;
    }
    return vm_get_phys_in_root(space->root_phys, virt);
}

uint64_t address_space_get_flags(const AddressSpace* space, uint64_t virt) {
    if (space == 0 || space->root_phys == 0) {
        return 0;
    }
    return vm_get_flags_in_root(space->root_phys, virt);
}
