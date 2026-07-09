#include "kernel/mm/arch_vm.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vm.h"

static uint64_t* pml4_table = 0;
static uint64_t pml4_phys = 0;

static inline uint16_t pml4_index(uint64_t virt) {
    return (virt >> 39) & 0x1FF;
}

static inline uint16_t pdpt_index(uint64_t virt) {
    return (virt >> 30) & 0x1FF;
}

static inline uint16_t pd_index(uint64_t virt) {
    return (virt >> 21) & 0x1FF;
}

static inline uint16_t pt_index(uint64_t virt) {
    return (virt >> 12) & 0x1FF;
}

static void zero_table(uint64_t* table) {
    for (int i = 0; i < 512; i++) {
        table[i] = 0;
    }
}

static uint64_t* entry_to_table(uint64_t entry) {
    return (uint64_t*)(uintptr_t)(entry & 0x000FFFFFFFFFF000ULL);
}

static uint64_t read_msr(uint32_t msr) {
    uint32_t lo;
    uint32_t hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void write_msr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

static uint64_t allocate_table() {
    uint64_t phys = (uint64_t)(uintptr_t)pmm_alloc_block();
    if (phys == 0) {
        return 0;
    }

    zero_table((uint64_t*)(uintptr_t)phys);
    return phys;
}

static int split_huge_page(uint64_t* pd, uint16_t index) {
    uint64_t entry = pd[index];
    if (!(entry & VM_FLAG_PRESENT) || !(entry & VM_FLAG_HUGE)) {
        return 0;
    }

    uint64_t pt_phys = allocate_table();
    if (pt_phys == 0) {
        return 0;
    }

    uint64_t* pt = (uint64_t*)(uintptr_t)pt_phys;
    uint64_t base = entry & 0x000FFFFFFFE00000ULL;
    uint64_t flags = entry & (0xFFFULL | VM_FLAG_NO_EXECUTE);
    flags &= ~VM_FLAG_HUGE;
    for (uint32_t i = 0; i < 512; i++) {
        pt[i] = (base + ((uint64_t)i * VM_PAGE_SIZE)) | flags;
    }

    pd[index] = pt_phys |
        VM_FLAG_PRESENT |
        VM_FLAG_WRITABLE |
        (entry & VM_FLAG_USER);
    return 1;
}

static uint64_t* get_or_create_next(uint64_t* table, uint16_t index, uint64_t flags) {
    uint64_t entry = table[index];
    if (entry & VM_FLAG_PRESENT) {
        if (entry & VM_FLAG_HUGE) {
            return 0;
        }
        if ((flags & VM_FLAG_USER) && !(entry & VM_FLAG_USER)) {
            table[index] = entry | VM_FLAG_USER;
        }
        return entry_to_table(entry);
    }

    uint64_t phys = allocate_table();
    if (phys == 0) {
        return 0;
    }

    table[index] = phys | VM_FLAG_PRESENT | VM_FLAG_WRITABLE | (flags & VM_FLAG_USER);
    return (uint64_t*)(uintptr_t)phys;
}

extern "C" void arch_vm_init() {
    __asm__ volatile("mov %%cr3, %0" : "=r"(pml4_phys));
    pml4_phys &= 0x000FFFFFFFFFF000ULL;
    pml4_table = (uint64_t*)(uintptr_t)pml4_phys;
}

extern "C" void arch_vm_enable_execute_disable() {
    const uint32_t IA32_EFER = 0xC0000080U;
    const uint64_t IA32_EFER_NXE = 1ULL << 11;
    uint64_t efer = read_msr(IA32_EFER);
    if (!(efer & IA32_EFER_NXE)) {
        write_msr(IA32_EFER, efer | IA32_EFER_NXE);
    }
}

extern "C" int arch_vm_is_execute_disable_enabled() {
    const uint32_t IA32_EFER = 0xC0000080U;
    const uint64_t IA32_EFER_NXE = 1ULL << 11;
    return (read_msr(IA32_EFER) & IA32_EFER_NXE) ? 1 : 0;
}

extern "C" int arch_vm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    if (pml4_table == 0) {
        arch_vm_init();
    }

    uint64_t* pdpt = get_or_create_next(pml4_table, pml4_index(virt), flags);
    if (pdpt == 0) {
        return 0;
    }

    uint64_t* pd = get_or_create_next(pdpt, pdpt_index(virt), flags);
    if (pd == 0) {
        return 0;
    }

    uint64_t entry = pd[pd_index(virt)];
    uint64_t* pt = 0;
    if (entry & VM_FLAG_PRESENT) {
        if (entry & VM_FLAG_HUGE) {
            if (!split_huge_page(pd, pd_index(virt))) {
                return 0;
            }
            entry = pd[pd_index(virt)];
        }
        if ((flags & VM_FLAG_USER) && !(entry & VM_FLAG_USER)) {
            pd[pd_index(virt)] = entry | VM_FLAG_USER;
        }
        pt = entry_to_table(entry);
    } else {
        uint64_t pt_phys = allocate_table();
        if (pt_phys == 0) {
            return 0;
        }
        pd[pd_index(virt)] = pt_phys | VM_FLAG_PRESENT | VM_FLAG_WRITABLE | (flags & VM_FLAG_USER);
        pt = (uint64_t*)(uintptr_t)pt_phys;
    }

    pt[pt_index(virt)] = (phys & 0x000FFFFFFFFFF000ULL) | flags | VM_FLAG_PRESENT;
    arch_vm_flush_page(virt);
    return 1;
}

extern "C" int arch_vm_unmap_page(uint64_t virt) {
    if (pml4_table == 0) {
        arch_vm_init();
    }

    uint64_t pml4e = pml4_table[pml4_index(virt)];
    if (!(pml4e & VM_FLAG_PRESENT)) {
        return 0;
    }

    uint64_t* pdpt = entry_to_table(pml4e);
    uint64_t pdpte = pdpt[pdpt_index(virt)];
    if (!(pdpte & VM_FLAG_PRESENT) || (pdpte & VM_FLAG_HUGE)) {
        return 0;
    }

    uint64_t* pd = entry_to_table(pdpte);
    uint64_t pde = pd[pd_index(virt)];
    if (!(pde & VM_FLAG_PRESENT) || (pde & VM_FLAG_HUGE)) {
        return 0;
    }

    uint64_t* pt = entry_to_table(pde);
    uint64_t pte = pt[pt_index(virt)];
    if (!(pte & VM_FLAG_PRESENT)) {
        return 0;
    }

    pt[pt_index(virt)] = 0;
    arch_vm_flush_page(virt);
    return 1;
}

extern "C" uint64_t arch_vm_get_phys(uint64_t virt) {
    if (pml4_table == 0) {
        arch_vm_init();
    }

    uint64_t pml4e = pml4_table[pml4_index(virt)];
    if (!(pml4e & VM_FLAG_PRESENT)) {
        return 0;
    }

    uint64_t* pdpt = entry_to_table(pml4e);
    uint64_t pdpte = pdpt[pdpt_index(virt)];
    if (!(pdpte & VM_FLAG_PRESENT)) {
        return 0;
    }
    if (pdpte & VM_FLAG_HUGE) {
        uint64_t base = pdpte & 0x000FFFFFC0000000ULL;
        return base | (virt & 0x3FFFFFFFULL);
    }

    uint64_t* pd = entry_to_table(pdpte);
    uint64_t pde = pd[pd_index(virt)];
    if (!(pde & VM_FLAG_PRESENT)) {
        return 0;
    }
    if (pde & VM_FLAG_HUGE) {
        uint64_t base = pde & 0x000FFFFFFFE00000ULL;
        return base | (virt & 0x1FFFFFULL);
    }

    uint64_t* pt = entry_to_table(pde);
    uint64_t pte = pt[pt_index(virt)];
    if (!(pte & VM_FLAG_PRESENT)) {
        return 0;
    }

    return (pte & 0x000FFFFFFFFFF000ULL) | (virt & 0xFFFULL);
}

extern "C" uint64_t arch_vm_get_flags(uint64_t virt) {
    if (pml4_table == 0) {
        arch_vm_init();
    }

    uint64_t pml4e = pml4_table[pml4_index(virt)];
    if (!(pml4e & VM_FLAG_PRESENT)) {
        return 0;
    }

    uint64_t access = VM_FLAG_USER | VM_FLAG_WRITABLE;
    access &= pml4e;
    uint64_t* pdpt = entry_to_table(pml4e);
    uint64_t pdpte = pdpt[pdpt_index(virt)];
    if (!(pdpte & VM_FLAG_PRESENT)) {
        return 0;
    }
    access &= pdpte;
    if (pdpte & VM_FLAG_HUGE) {
        return VM_FLAG_PRESENT | access | (pdpte & VM_FLAG_NO_EXECUTE);
    }

    uint64_t* pd = entry_to_table(pdpte);
    uint64_t pde = pd[pd_index(virt)];
    if (!(pde & VM_FLAG_PRESENT)) {
        return 0;
    }
    access &= pde;
    if (pde & VM_FLAG_HUGE) {
        return VM_FLAG_PRESENT | access | (pde & VM_FLAG_NO_EXECUTE);
    }

    uint64_t* pt = entry_to_table(pde);
    uint64_t pte = pt[pt_index(virt)];
    if (!(pte & VM_FLAG_PRESENT)) {
        return 0;
    }
    access &= pte;
    return VM_FLAG_PRESENT | access | (pte & VM_FLAG_NO_EXECUTE);
}

extern "C" void arch_vm_flush_page(uint64_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"((void*)(uintptr_t)virt) : "memory");
}

extern "C" uint64_t arch_vm_get_root_phys() {
    if (pml4_table == 0) {
        arch_vm_init();
    }
    return pml4_phys;
}
