#include "kernel/mm/vm.h"

#include "kernel/mm/arch_vm.h"
#include "kernel/mm/pmm.h"

static uint64_t align_down_page(uint64_t value) {
    return value & ~(VM_PAGE_SIZE - 1ULL);
}

static uint64_t align_up_page(uint64_t value) {
    return (value + VM_PAGE_SIZE - 1ULL) & ~(VM_PAGE_SIZE - 1ULL);
}

extern "C" void vm_init() {
    arch_vm_init();
}

extern "C" void vm_enable_execute_disable() {
    arch_vm_enable_execute_disable();
}

extern "C" int vm_is_execute_disable_enabled() {
    return arch_vm_is_execute_disable_enabled();
}

extern "C" int vm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    return arch_vm_map_page(virt, phys, flags);
}

extern "C" int vm_map_page_in_root(uint64_t root_phys, uint64_t virt, uint64_t phys, uint64_t flags) {
    return arch_vm_map_page_in_root(root_phys, virt, phys, flags);
}

extern "C" int vm_unmap_page(uint64_t virt) {
    return arch_vm_unmap_page(virt);
}

extern "C" int vm_unmap_page_in_root(uint64_t root_phys, uint64_t virt) {
    return arch_vm_unmap_page_in_root(root_phys, virt);
}

extern "C" int vm_map_identity(uint64_t start, uint64_t size, uint64_t flags) {
    if (size == 0) {
        return 1;
    }

    uint64_t begin = align_down_page(start);
    uint64_t end = align_up_page(start + size);
    if (end < begin) {
        return 0;
    }

    for (uint64_t addr = begin; addr < end; addr += VM_PAGE_SIZE) {
        if (!vm_map_page(addr, addr, flags)) {
            return 0;
        }
    }
    return 1;
}

extern "C" int vm_map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags) {
    if (size == 0) {
        return 1;
    }

    uint64_t virt_begin = align_down_page(virt);
    uint64_t phys_begin = align_down_page(phys);
    uint64_t end = align_up_page(virt + size);
    if (end < virt_begin) {
        return 0;
    }

    uint32_t mapped = 0;
    for (uint64_t addr = virt_begin; addr < end; addr += VM_PAGE_SIZE) {
        uint64_t page_phys = phys_begin + ((uint64_t)mapped * VM_PAGE_SIZE);
        if (!vm_map_page(addr, page_phys, flags)) {
            for (uint32_t rollback = 0; rollback < mapped; rollback++) {
                vm_unmap_page(virt_begin + ((uint64_t)rollback * VM_PAGE_SIZE));
            }
            return 0;
        }
        mapped++;
    }
    return 1;
}

extern "C" int vm_protect_range(uint64_t virt, uint64_t size, uint64_t flags) {
    if (size == 0) {
        return 1;
    }

    uint64_t begin = align_down_page(virt);
    uint64_t end = align_up_page(virt + size);
    if (end < begin) {
        return 0;
    }

    for (uint64_t addr = begin; addr < end; addr += VM_PAGE_SIZE) {
        uint64_t phys = vm_get_phys(addr) & ~(VM_PAGE_SIZE - 1ULL);
        if (phys == 0 || !vm_map_page(addr, phys, flags)) {
            return 0;
        }
    }
    return 1;
}

extern "C" int vm_protect_range_in_root(uint64_t root_phys, uint64_t virt, uint64_t size, uint64_t flags) {
    if (size == 0) {
        return 1;
    }

    uint64_t begin = align_down_page(virt);
    uint64_t end = align_up_page(virt + size);
    if (end < begin) {
        return 0;
    }

    for (uint64_t addr = begin; addr < end; addr += VM_PAGE_SIZE) {
        uint64_t phys = vm_get_phys_in_root(root_phys, addr) & ~(VM_PAGE_SIZE - 1ULL);
        if (phys == 0 || !vm_map_page_in_root(root_phys, addr, phys, flags)) {
            return 0;
        }
    }
    return 1;
}

extern "C" int vm_alloc_map_range(
    uint64_t virt,
    uint64_t size,
    uint64_t flags,
    uint32_t* out_page_count
) {
    if (out_page_count != 0) {
        *out_page_count = 0;
    }
    if (size == 0) {
        return 1;
    }

    uint64_t begin = align_down_page(virt);
    uint64_t end = align_up_page(virt + size);
    if (end < begin) {
        return 0;
    }

    uint32_t mapped = 0;
    for (uint64_t addr = begin; addr < end; addr += VM_PAGE_SIZE) {
        uint64_t phys = (uint64_t)(uintptr_t)pmm_alloc_block();
        if (phys == 0) {
            vm_unmap_free_range(begin, mapped);
            return 0;
        }

        if (!vm_map_page(addr, phys, flags)) {
            pmm_free_block((void*)(uintptr_t)phys);
            vm_unmap_free_range(begin, mapped);
            return 0;
        }
        mapped++;
    }

    if (out_page_count != 0) {
        *out_page_count = mapped;
    }
    return 1;
}

extern "C" int vm_alloc_map_range_in_root(
    uint64_t root_phys,
    uint64_t virt,
    uint64_t size,
    uint64_t flags,
    uint32_t* out_page_count
) {
    if (out_page_count != 0) {
        *out_page_count = 0;
    }
    if (size == 0) {
        return 1;
    }

    uint64_t begin = align_down_page(virt);
    uint64_t end = align_up_page(virt + size);
    if (end < begin) {
        return 0;
    }

    uint32_t mapped = 0;
    for (uint64_t addr = begin; addr < end; addr += VM_PAGE_SIZE) {
        uint64_t phys = (uint64_t)(uintptr_t)pmm_alloc_block();
        if (phys == 0) {
            vm_unmap_free_range_in_root(root_phys, begin, mapped);
            return 0;
        }

        if (!vm_map_page_in_root(root_phys, addr, phys, flags)) {
            pmm_free_block((void*)(uintptr_t)phys);
            vm_unmap_free_range_in_root(root_phys, begin, mapped);
            return 0;
        }
        mapped++;
    }

    if (out_page_count != 0) {
        *out_page_count = mapped;
    }
    return 1;
}

extern "C" uint32_t vm_unmap_free_range(uint64_t virt, uint32_t page_count) {
    uint64_t begin = align_down_page(virt);
    uint32_t unmapped = 0;

    for (uint32_t page = 0; page < page_count; page++) {
        uint64_t addr = begin + ((uint64_t)page * VM_PAGE_SIZE);
        uint64_t phys = vm_get_phys(addr) & ~(VM_PAGE_SIZE - 1ULL);
        if (vm_unmap_page(addr)) {
            unmapped++;
            if (phys != 0) {
                pmm_free_block((void*)(uintptr_t)phys);
            }
        }
    }
    return unmapped;
}

extern "C" uint32_t vm_unmap_free_range_in_root(uint64_t root_phys, uint64_t virt, uint32_t page_count) {
    uint64_t begin = align_down_page(virt);
    uint32_t unmapped = 0;

    for (uint32_t page = 0; page < page_count; page++) {
        uint64_t addr = begin + ((uint64_t)page * VM_PAGE_SIZE);
        uint64_t phys = vm_get_phys_in_root(root_phys, addr) & ~(VM_PAGE_SIZE - 1ULL);
        if (vm_unmap_page_in_root(root_phys, addr)) {
            unmapped++;
            if (phys != 0) {
                pmm_free_block((void*)(uintptr_t)phys);
            }
        }
    }
    return unmapped;
}

extern "C" uint64_t vm_get_phys(uint64_t virt) {
    return arch_vm_get_phys(virt);
}

extern "C" uint64_t vm_get_phys_in_root(uint64_t root_phys, uint64_t virt) {
    return arch_vm_get_phys_in_root(root_phys, virt);
}

extern "C" uint64_t vm_get_flags(uint64_t virt) {
    return arch_vm_get_flags(virt);
}

extern "C" uint64_t vm_get_flags_in_root(uint64_t root_phys, uint64_t virt) {
    return arch_vm_get_flags_in_root(root_phys, virt);
}

extern "C" void vm_flush_page(uint64_t virt) {
    arch_vm_flush_page(virt);
}

extern "C" uint64_t vm_get_root_phys() {
    return arch_vm_get_root_phys();
}

extern "C" uint64_t vm_create_root() {
    return arch_vm_create_root();
}

extern "C" void vm_switch_root(uint64_t root_phys) {
    arch_vm_switch_root(root_phys);
}
