#include "arch/x86_64/paging64.h"
#include "arch/x86_64/pmm64.h"

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

static uint64_t align_down_page(uint64_t value) {
    return value & ~(PAGING64_PAGE_SIZE - 1ULL);
}

static uint64_t align_up_page(uint64_t value) {
    return (value + PAGING64_PAGE_SIZE - 1ULL) & ~(PAGING64_PAGE_SIZE - 1ULL);
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
    uint64_t phys = (uint64_t)(uintptr_t)pmm64_alloc_block();
    if (phys == 0) {
        return 0;
    }

    zero_table((uint64_t*)(uintptr_t)phys);
    return phys;
}

static int split_huge_page(uint64_t* pd, uint16_t index) {
    uint64_t entry = pd[index];
    if (!(entry & PAGING64_FLAG_PRESENT) || !(entry & PAGING64_FLAG_HUGE)) {
        return 0;
    }

    uint64_t pt_phys = allocate_table();
    if (pt_phys == 0) {
        return 0;
    }

    uint64_t* pt = (uint64_t*)(uintptr_t)pt_phys;
    uint64_t base = entry & 0x000FFFFFFFE00000ULL;
    uint64_t flags = entry & (0xFFFULL | PAGING64_FLAG_NX);
    flags &= ~PAGING64_FLAG_HUGE;
    for (uint32_t i = 0; i < 512; i++) {
        pt[i] = (base + ((uint64_t)i * PAGING64_PAGE_SIZE)) | flags;
    }

    pd[index] = pt_phys |
        PAGING64_FLAG_PRESENT |
        PAGING64_FLAG_WRITABLE |
        (entry & PAGING64_FLAG_USER);
    return 1;
}

static uint64_t* get_or_create_next(uint64_t* table, uint16_t index, uint64_t flags) {
    uint64_t entry = table[index];
    if (entry & PAGING64_FLAG_PRESENT) {
        if (entry & PAGING64_FLAG_HUGE) {
            return 0;
        }
        if ((flags & PAGING64_FLAG_USER) && !(entry & PAGING64_FLAG_USER)) {
            table[index] = entry | PAGING64_FLAG_USER;
        }
        return entry_to_table(entry);
    }

    uint64_t phys = allocate_table();
    if (phys == 0) {
        return 0;
    }

    table[index] = phys | PAGING64_FLAG_PRESENT | PAGING64_FLAG_WRITABLE | (flags & PAGING64_FLAG_USER);
    return (uint64_t*)(uintptr_t)phys;
}

extern "C" void paging64_init() {
    __asm__ volatile("mov %%cr3, %0" : "=r"(pml4_phys));
    pml4_phys &= 0x000FFFFFFFFFF000ULL;
    pml4_table = (uint64_t*)(uintptr_t)pml4_phys;
}

extern "C" void paging64_enable_nxe() {
    const uint32_t IA32_EFER = 0xC0000080U;
    const uint64_t IA32_EFER_NXE = 1ULL << 11;
    uint64_t efer = read_msr(IA32_EFER);
    if (!(efer & IA32_EFER_NXE)) {
        write_msr(IA32_EFER, efer | IA32_EFER_NXE);
    }
}

extern "C" int paging64_is_nxe_enabled() {
    const uint32_t IA32_EFER = 0xC0000080U;
    const uint64_t IA32_EFER_NXE = 1ULL << 11;
    return (read_msr(IA32_EFER) & IA32_EFER_NXE) ? 1 : 0;
}

extern "C" int paging64_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    if (pml4_table == 0) {
        paging64_init();
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
    if (entry & PAGING64_FLAG_PRESENT) {
        if (entry & PAGING64_FLAG_HUGE) {
            if (!split_huge_page(pd, pd_index(virt))) {
                return 0;
            }
            entry = pd[pd_index(virt)];
        }
        if ((flags & PAGING64_FLAG_USER) && !(entry & PAGING64_FLAG_USER)) {
            pd[pd_index(virt)] = entry | PAGING64_FLAG_USER;
        }
        pt = entry_to_table(entry);
    } else {
        uint64_t pt_phys = allocate_table();
        if (pt_phys == 0) {
            return 0;
        }
        pd[pd_index(virt)] = pt_phys | PAGING64_FLAG_PRESENT | PAGING64_FLAG_WRITABLE | (flags & PAGING64_FLAG_USER);
        pt = (uint64_t*)(uintptr_t)pt_phys;
    }

    pt[pt_index(virt)] = (phys & 0x000FFFFFFFFFF000ULL) | flags | PAGING64_FLAG_PRESENT;
    paging64_flush_tlb(virt);
    return 1;
}

extern "C" int paging64_unmap_page(uint64_t virt) {
    if (pml4_table == 0) {
        paging64_init();
    }

    uint64_t pml4e = pml4_table[pml4_index(virt)];
    if (!(pml4e & PAGING64_FLAG_PRESENT)) {
        return 0;
    }

    uint64_t* pdpt = entry_to_table(pml4e);
    uint64_t pdpte = pdpt[pdpt_index(virt)];
    if (!(pdpte & PAGING64_FLAG_PRESENT) || (pdpte & PAGING64_FLAG_HUGE)) {
        return 0;
    }

    uint64_t* pd = entry_to_table(pdpte);
    uint64_t pde = pd[pd_index(virt)];
    if (!(pde & PAGING64_FLAG_PRESENT) || (pde & PAGING64_FLAG_HUGE)) {
        return 0;
    }

    uint64_t* pt = entry_to_table(pde);
    uint64_t pte = pt[pt_index(virt)];
    if (!(pte & PAGING64_FLAG_PRESENT)) {
        return 0;
    }

    pt[pt_index(virt)] = 0;
    paging64_flush_tlb(virt);
    return 1;
}

extern "C" int paging64_map_range_identity(uint64_t start, uint64_t size, uint64_t flags) {
    if (size == 0) {
        return 1;
    }

    uint64_t begin = align_down_page(start);
    uint64_t end = align_up_page(start + size);
    if (end < begin) {
        return 0;
    }

    for (uint64_t addr = begin; addr < end; addr += PAGING64_PAGE_SIZE) {
        if (!paging64_map_page(addr, addr, flags)) {
            return 0;
        }
    }
    return 1;
}

extern "C" int paging64_map_range(uint64_t virt, uint64_t phys, uint64_t size, uint64_t flags) {
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
    for (uint64_t addr = virt_begin; addr < end; addr += PAGING64_PAGE_SIZE) {
        uint64_t page_phys = phys_begin + ((uint64_t)mapped * PAGING64_PAGE_SIZE);
        if (!paging64_map_page(addr, page_phys, flags)) {
            for (uint32_t rollback = 0; rollback < mapped; rollback++) {
                paging64_unmap_page(virt_begin + ((uint64_t)rollback * PAGING64_PAGE_SIZE));
            }
            return 0;
        }
        mapped++;
    }

    return 1;
}

extern "C" int paging64_remap_range(uint64_t virt, uint64_t size, uint64_t flags) {
    if (size == 0) {
        return 1;
    }

    uint64_t begin = align_down_page(virt);
    uint64_t end = align_up_page(virt + size);
    if (end < begin) {
        return 0;
    }

    for (uint64_t addr = begin; addr < end; addr += PAGING64_PAGE_SIZE) {
        uint64_t phys = paging64_get_phys(addr) & 0x000FFFFFFFFFF000ULL;
        if (phys == 0 || !paging64_map_page(addr, phys, flags)) {
            return 0;
        }
    }
    return 1;
}

extern "C" int paging64_alloc_map_range(uint64_t virt, uint64_t size, uint64_t flags, uint32_t* out_page_count) {
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
    for (uint64_t addr = begin; addr < end; addr += PAGING64_PAGE_SIZE) {
        uint64_t phys = (uint64_t)(uintptr_t)pmm64_alloc_block();
        if (phys == 0) {
            paging64_unmap_free_range(begin, mapped);
            return 0;
        }

        if (!paging64_map_page(addr, phys, flags)) {
            pmm64_free_block((void*)(uintptr_t)phys);
            paging64_unmap_free_range(begin, mapped);
            return 0;
        }
        mapped++;
    }

    if (out_page_count != 0) {
        *out_page_count = mapped;
    }
    return 1;
}

extern "C" uint32_t paging64_unmap_free_range(uint64_t virt, uint32_t page_count) {
    uint64_t begin = align_down_page(virt);
    uint32_t unmapped = 0;

    for (uint32_t page = 0; page < page_count; page++) {
        uint64_t addr = begin + ((uint64_t)page * PAGING64_PAGE_SIZE);
        uint64_t phys = paging64_get_phys(addr) & 0x000FFFFFFFFFF000ULL;
        if (paging64_unmap_page(addr)) {
            unmapped++;
            if (phys != 0) {
                pmm64_free_block((void*)(uintptr_t)phys);
            }
        }
    }

    return unmapped;
}

extern "C" uint64_t paging64_get_phys(uint64_t virt) {
    if (pml4_table == 0) {
        paging64_init();
    }

    uint64_t pml4e = pml4_table[pml4_index(virt)];
    if (!(pml4e & PAGING64_FLAG_PRESENT)) {
        return 0;
    }

    uint64_t* pdpt = entry_to_table(pml4e);
    uint64_t pdpte = pdpt[pdpt_index(virt)];
    if (!(pdpte & PAGING64_FLAG_PRESENT)) {
        return 0;
    }
    if (pdpte & PAGING64_FLAG_HUGE) {
        uint64_t base = pdpte & 0x000FFFFFC0000000ULL;
        return base | (virt & 0x3FFFFFFFULL);
    }

    uint64_t* pd = entry_to_table(pdpte);
    uint64_t pde = pd[pd_index(virt)];
    if (!(pde & PAGING64_FLAG_PRESENT)) {
        return 0;
    }
    if (pde & PAGING64_FLAG_HUGE) {
        uint64_t base = pde & 0x000FFFFFFFE00000ULL;
        return base | (virt & 0x1FFFFFULL);
    }

    uint64_t* pt = entry_to_table(pde);
    uint64_t pte = pt[pt_index(virt)];
    if (!(pte & PAGING64_FLAG_PRESENT)) {
        return 0;
    }

    return (pte & 0x000FFFFFFFFFF000ULL) | (virt & 0xFFFULL);
}

extern "C" void paging64_flush_tlb(uint64_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"((void*)(uintptr_t)virt) : "memory");
}

extern "C" uint64_t paging64_get_root_phys() {
    if (pml4_table == 0) {
        paging64_init();
    }
    return pml4_phys;
}
