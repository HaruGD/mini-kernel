#include "kernel/mm/address_space.h"

#include "kernel/cpu_local.h"
#include "kernel/mm/pmm.h"
#include "kernel/smp.h"

static volatile uint64_t next_address_space_identity = 1;

struct AddressSpaceInvalidation {
    uint64_t generation;
    uint64_t token;
    uint64_t address;
    uint32_t page_count;
    uint32_t target_mask;
    uint8_t full_flush;
};

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

static uint64_t allocate_identity() {
    uint64_t identity =
        __atomic_fetch_add(&next_address_space_identity, 1u, __ATOMIC_RELAXED);
    if (identity == 0) {
        identity =
            __atomic_fetch_add(&next_address_space_identity, 1u, __ATOMIC_RELAXED);
    }
    return identity;
}

static void reset_regions_unlocked(AddressSpace* space) {
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
        space->regions[i].reserved0 = 0;
        space->regions[i].reserved1 = 0;
        space->regions[i].rights = 0;
        space->regions[i].start = 0;
        space->regions[i].end = 0;
    }
}

static int ensure_root_unlocked(AddressSpace* space) {
    if (space->root_phys == 0) {
        space->root_phys = vm_create_root();
    }
    return space->root_phys != 0;
}

static void mutation_gate_release(AddressSpace* space) {
    __atomic_store_n(&space->shootdown_active, 0u, __ATOMIC_RELEASE);
}

static int mutation_gate_enter(AddressSpace* space) {
    if (space == 0 ||
        __atomic_load_n(&space->shootdown_timeout_count,
                        __ATOMIC_ACQUIRE) != 0) {
        return 0;
    }

    for (;;) {
        uint32_t expected = 0;
        if (__atomic_compare_exchange_n(&space->shootdown_active,
                                        &expected,
                                        1u,
                                        0,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            if (__atomic_load_n(&space->shootdown_timeout_count,
                                __ATOMIC_ACQUIRE) == 0) {
                while (__atomic_load_n(&space->active_user_accesses,
                                       __ATOMIC_ACQUIRE) != 0) {
                    __asm__ volatile("pause");
                }
                return 1;
            }
            mutation_gate_release(space);
            return 0;
        }

#ifdef OS64_HOST_TEST
        while (__atomic_load_n(&space->shootdown_active,
                               __ATOMIC_ACQUIRE) != 0) {
            __asm__ volatile("pause");
        }
#else
        uint64_t saved_flags = 0;
        __asm__ volatile("pushfq; pop %0" : "=r"(saved_flags));
        if ((saved_flags & (1ULL << 9)) == 0) {
            __asm__ volatile("sti" : : : "memory");
        }
        if (!kernel_tlb_wait_enter()) {
            if ((saved_flags & (1ULL << 9)) == 0) {
                __asm__ volatile("cli" : : : "memory");
            }
            return 0;
        }
        while (__atomic_load_n(&space->shootdown_active,
                               __ATOMIC_ACQUIRE) != 0) {
            __asm__ volatile("pause");
        }
        kernel_tlb_wait_leave();
        if ((saved_flags & (1ULL << 9)) == 0) {
            __asm__ volatile("cli" : : : "memory");
        }
#endif
    }
}

static AddressSpaceInvalidation begin_invalidation_unlocked(
    AddressSpace* space,
    uint64_t address,
    uint32_t page_count,
    int full_flush
) {
    AddressSpaceInvalidation invalidation = {};
    uint64_t generation =
        __atomic_add_fetch(&space->tlb_generation, 1u, __ATOMIC_ACQ_REL);
    if (generation == 0) {
        generation =
            __atomic_add_fetch(&space->tlb_generation, 1u, __ATOMIC_ACQ_REL);
    }
    uint64_t token =
        __atomic_add_fetch(&space->operation_token, 1u, __ATOMIC_ACQ_REL);
    if (token == 0) {
        token =
            __atomic_add_fetch(&space->operation_token, 1u, __ATOMIC_ACQ_REL);
    }
    invalidation.generation = generation;
    invalidation.token = token;
    invalidation.address = align_down_page(address);
    invalidation.page_count = page_count;
    invalidation.target_mask =
        __atomic_load_n(&space->cached_cpu_mask, __ATOMIC_ACQUIRE);
    invalidation.full_flush =
        full_flush || page_count == 0 ||
        page_count > ADDRESS_SPACE_TLB_PAGE_LIMIT;
    return invalidation;
}

static int complete_invalidation(AddressSpace* space,
                                 const AddressSpaceInvalidation& invalidation) {
    uint32_t acknowledged = 0;
    int completed = 1;
    if (invalidation.target_mask != 0) {
        completed = smp_tlb_shootdown(space,
                                      invalidation.address,
                                      invalidation.page_count,
                                      invalidation.full_flush,
                                      invalidation.generation,
                                      invalidation.token,
                                      invalidation.target_mask,
                                      &acknowledged);
    }

    KernelSpinlockToken lock_token;
    if (!kernel_spinlock_acquire(&space->lock, &lock_token)) {
        mutation_gate_release(space);
        return 0;
    }
    const int token_valid =
        __atomic_load_n(&space->operation_token, __ATOMIC_ACQUIRE) ==
            invalidation.token &&
        __atomic_load_n(&space->tlb_generation, __ATOMIC_ACQUIRE) ==
            invalidation.generation;
    if (completed && token_valid &&
        (acknowledged & invalidation.target_mask) ==
            invalidation.target_mask) {
        space->shootdown_count++;
    } else {
        space->shootdown_timeout_count++;
        completed = 0;
    }
    mutation_gate_release(space);
    kernel_spinlock_release(&space->lock, &lock_token);
    return completed;
}

void address_space_init(AddressSpace* space) {
    if (space == 0) {
        return;
    }
    space->root_phys = 0;
    kernel_spinlock_init(&space->lock,
                         KERNEL_LOCK_CLASS_ADDRESS_SPACE,
                         "address_space");
    space->identity = allocate_identity();
    space->tlb_generation = 1;
    space->operation_token = 0;
    space->active_cpu_mask = 0;
    space->cached_cpu_mask = 0;
    space->shootdown_active = 0;
    space->active_user_accesses = 0;
    space->shootdown_count = 0;
    space->shootdown_timeout_count = 0;
    space->quarantined_page_count = 0;
    space->retired_page_count = 0;
    reset_regions_unlocked(space);
}

void address_space_recycle(AddressSpace* space) {
    if (space == 0) {
        return;
    }
    const uint64_t root = space->root_phys;
    kernel_spinlock_init(&space->lock,
                         KERNEL_LOCK_CLASS_ADDRESS_SPACE,
                         "address_space");
    space->root_phys = root;
    space->identity = allocate_identity();
    space->tlb_generation = 1;
    space->operation_token = 0;
    space->active_cpu_mask = 0;
    space->cached_cpu_mask = 0;
    space->shootdown_active = 0;
    space->active_user_accesses = 0;
    space->shootdown_count = 0;
    space->shootdown_timeout_count = 0;
    space->quarantined_page_count = 0;
    space->retired_page_count = 0;
    reset_regions_unlocked(space);
}

int address_space_ensure_root(AddressSpace* space) {
    if (space == 0) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&space->lock, &token)) {
        return 0;
    }
    const int result = ensure_root_unlocked(space);
    kernel_spinlock_release(&space->lock, &token);
    return result;
}

void address_space_reset_user(AddressSpace* space) {
    if (space == 0) {
        return;
    }
    if (!mutation_gate_enter(space)) {
        return;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&space->lock, &token)) {
        mutation_gate_release(space);
        return;
    }
    reset_regions_unlocked(space);
    kernel_spinlock_release(&space->lock, &token);
    mutation_gate_release(space);
}

void address_space_activate(const AddressSpace* const_space) {
    if (const_space == 0 || const_space->root_phys == 0) {
        address_space_activate_kernel();
        return;
    }
    AddressSpace* space = (AddressSpace*)const_space;
#ifndef OS64_HOST_TEST
    CpuLocal* local = cpu_local_current();
    if (cpu_local_validate(local)) {
        const uint32_t bit = 1u << local->logical_id;
        AddressSpace* previous = local->loaded_address_space;
        if (previous != 0 && previous != space) {
            __atomic_fetch_and(&previous->active_cpu_mask,
                               ~bit,
                               __ATOMIC_ACQ_REL);
        }
        __atomic_fetch_or(&space->active_cpu_mask, bit, __ATOMIC_ACQ_REL);
        __atomic_fetch_or(&space->cached_cpu_mask, bit, __ATOMIC_ACQ_REL);
        const uint64_t generation =
            __atomic_load_n(&space->tlb_generation, __ATOMIC_ACQUIRE);
        vm_switch_root(space->root_phys);
        local->loaded_address_space = space;
        local->loaded_address_space_identity = space->identity;
        local->loaded_address_space_root = space->root_phys;
        __atomic_store_n(&local->observed_tlb_generation,
                         generation,
                         __ATOMIC_RELEASE);
        local->tlb_local_flush_count++;
        return;
    }
#endif
    vm_switch_root(space->root_phys);
}

void address_space_activate_kernel() {
#ifndef OS64_HOST_TEST
    CpuLocal* local = cpu_local_current();
    if (cpu_local_validate(local)) {
        AddressSpace* previous = local->loaded_address_space;
        if (previous != 0) {
            const uint32_t bit = 1u << local->logical_id;
            __atomic_fetch_and(&previous->active_cpu_mask,
                               ~bit,
                               __ATOMIC_ACQ_REL);
        }
        local->loaded_address_space = 0;
        local->loaded_address_space_identity = 0;
        local->loaded_address_space_root = 0;
        local->observed_tlb_generation = 0;
    }
#endif
    vm_switch_root(vm_get_root_phys());
}

int address_space_add_region(AddressSpace* space,
                             uint64_t start,
                             uint64_t size,
                             uint32_t rights) {
    if (space == 0 || size == 0) {
        return 1;
    }
    const uint64_t begin = align_down_page(start);
    const uint64_t end = align_up_page(start + size);
    if (end <= begin) {
        return 0;
    }

    if (!mutation_gate_enter(space)) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&space->lock, &token)) {
        mutation_gate_release(space);
        return 0;
    }
    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX_REGIONS; i++) {
        AddressSpaceRegion* region = &space->regions[i];
        if (!region->active) {
            continue;
        }
        if (region->end == begin && region->rights == rights) {
            region->end = end;
            kernel_spinlock_release(&space->lock, &token);
            mutation_gate_release(space);
            return 1;
        }
        if (region->start == end && region->rights == rights) {
            region->start = begin;
            kernel_spinlock_release(&space->lock, &token);
            mutation_gate_release(space);
            return 1;
        }
    }
    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX_REGIONS; i++) {
        AddressSpaceRegion* region = &space->regions[i];
        if (!region->active) {
            region->active = 1;
            region->rights = rights;
            region->start = begin;
            region->end = end;
            space->region_count++;
            kernel_spinlock_release(&space->lock, &token);
            mutation_gate_release(space);
            return 1;
        }
    }
    kernel_spinlock_release(&space->lock, &token);
    mutation_gate_release(space);
    return 0;
}

void address_space_remove_region(AddressSpace* space,
                                 uint64_t start,
                                 uint64_t size) {
    if (space == 0 || size == 0) {
        return;
    }
    const uint64_t begin = align_down_page(start);
    const uint64_t end = align_up_page(start + size);
    if (!mutation_gate_enter(space)) {
        return;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&space->lock, &token)) {
        mutation_gate_release(space);
        return;
    }
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
            if (!split->active) {
                split->active = 1;
                split->rights = region->rights;
                split->start = end;
                split->end = region->end;
                region->end = begin;
                space->region_count++;
                kernel_spinlock_release(&space->lock, &token);
                mutation_gate_release(space);
                return;
            }
        }
        region->end = begin;
        break;
    }
    kernel_spinlock_release(&space->lock, &token);
    mutation_gate_release(space);
}

static const AddressSpaceRegion* find_region(const AddressSpace* space,
                                             uint64_t address) {
    if (space == 0) {
        return 0;
    }
    for (uint32_t i = 0; i < ADDRESS_SPACE_MAX_REGIONS; i++) {
        const AddressSpaceRegion* region = &space->regions[i];
        if (region->active && address >= region->start &&
            address < region->end) {
            return region;
        }
    }
    return 0;
}

int address_space_owns_address(const AddressSpace* space, uint64_t address) {
    return find_region(space, address) != 0;
}

int address_space_buffer_accessible(const AddressSpace* space,
                                    uint64_t start,
                                    uint64_t size,
                                    int writable) {
    if (size == 0) {
        return 1;
    }
    if (space == 0 || start == 0) {
        return 0;
    }
    const uint64_t end = start + size - 1;
    if (end < start) {
        return 0;
    }
    AddressSpace* mutable_space = (AddressSpace*)space;
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&mutable_space->lock, &token)) {
        return 0;
    }
    int accessible = 1;
    uint64_t address = start;
    while (1) {
        const AddressSpaceRegion* region = find_region(space, address);
        if (region == 0 ||
            !(region->rights & ADDRESS_SPACE_REGION_READ) ||
            (writable && !(region->rights & ADDRESS_SPACE_REGION_WRITE))) {
            accessible = 0;
            break;
        }
        const uint64_t flags = address_space_get_flags(space, address);
        if (!(flags & VM_FLAG_USER) ||
            (writable && !(flags & VM_FLAG_WRITABLE))) {
            accessible = 0;
            break;
        }
        if ((address & ~(VM_PAGE_SIZE - 1ULL)) ==
            (end & ~(VM_PAGE_SIZE - 1ULL))) {
            break;
        }
        address = (address & ~(VM_PAGE_SIZE - 1ULL)) + VM_PAGE_SIZE;
    }
    if (accessible && find_region(space, end) == 0) {
        accessible = 0;
    }
    kernel_spinlock_release(&mutable_space->lock, &token);
    return accessible;
}

int address_space_user_access_begin(AddressSpace* space,
                                    uint64_t expected_identity) {
    if (space == 0 || expected_identity == 0 ||
        kernel_spinlock_depth() != 0 || kernel_in_tlb_wait() ||
        __atomic_load_n(&space->shootdown_timeout_count,
                        __ATOMIC_ACQUIRE) != 0) {
        return 0;
    }

    if (__atomic_load_n(&space->shootdown_active, __ATOMIC_ACQUIRE) != 0) {
        return 0;
    }
    __atomic_add_fetch(&space->active_user_accesses, 1u, __ATOMIC_ACQ_REL);
    if (__atomic_load_n(&space->shootdown_active, __ATOMIC_ACQUIRE) != 0 ||
        __atomic_load_n(&space->identity, __ATOMIC_ACQUIRE) !=
            expected_identity) {
        __atomic_sub_fetch(&space->active_user_accesses, 1u, __ATOMIC_RELEASE);
        return 0;
    }
    return 1;
}

void address_space_user_access_end(AddressSpace* space) {
    if (space == 0) {
        return;
    }
    uint32_t active =
        __atomic_load_n(&space->active_user_accesses, __ATOMIC_ACQUIRE);
    while (active != 0) {
        if (__atomic_compare_exchange_n(&space->active_user_accesses,
                                        &active,
                                        active - 1u,
                                        0,
                                        __ATOMIC_RELEASE,
                                        __ATOMIC_ACQUIRE)) {
            return;
        }
    }
}

int address_space_map_page(AddressSpace* space,
                           uint64_t virt,
                           uint64_t phys,
                           uint64_t flags) {
    if (space == 0) {
        return 0;
    }
    if (!mutation_gate_enter(space)) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&space->lock, &token)) {
        mutation_gate_release(space);
        return 0;
    }
    const int mapped =
        ensure_root_unlocked(space) &&
        vm_map_page_in_root(space->root_phys, virt, phys, flags);
    AddressSpaceInvalidation invalidation = {};
    if (mapped) {
        invalidation = begin_invalidation_unlocked(space, virt, 1, 0);
    }
    kernel_spinlock_release(&space->lock, &token);
    if (!mapped) {
        mutation_gate_release(space);
        return 0;
    }
    return complete_invalidation(space, invalidation);
}

int address_space_unmap_page(AddressSpace* space, uint64_t virt) {
    if (space == 0 || space->root_phys == 0) {
        return 0;
    }
    if (!mutation_gate_enter(space)) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&space->lock, &token)) {
        mutation_gate_release(space);
        return 0;
    }
    const int unmapped =
        vm_unmap_page_in_root(space->root_phys, virt);
    AddressSpaceInvalidation invalidation = {};
    if (unmapped) {
        invalidation = begin_invalidation_unlocked(space, virt, 1, 0);
    }
    kernel_spinlock_release(&space->lock, &token);
    if (!unmapped) {
        mutation_gate_release(space);
        return 0;
    }
    return complete_invalidation(space, invalidation);
}

int address_space_protect_range(AddressSpace* space,
                                uint64_t virt,
                                uint64_t size,
                                uint64_t flags) {
    if (space == 0) {
        return 0;
    }
    if (!mutation_gate_enter(space)) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&space->lock, &token)) {
        mutation_gate_release(space);
        return 0;
    }
    const int protected_range =
        ensure_root_unlocked(space) &&
        vm_protect_range_in_root(space->root_phys, virt, size, flags);
    AddressSpaceInvalidation invalidation = {};
    if (protected_range) {
        const uint64_t begin = align_down_page(virt);
        const uint64_t end = align_up_page(virt + size);
        const uint64_t pages = end > begin ? (end - begin) / VM_PAGE_SIZE : 0;
        invalidation = begin_invalidation_unlocked(
            space,
            begin,
            pages <= UINT32_MAX ? (uint32_t)pages : 0,
            pages > ADDRESS_SPACE_TLB_PAGE_LIMIT);
    }
    kernel_spinlock_release(&space->lock, &token);
    if (!protected_range) {
        mutation_gate_release(space);
        return 0;
    }
    return complete_invalidation(space, invalidation);
}

int address_space_alloc_map_range(AddressSpace* space,
                                  uint64_t virt,
                                  uint64_t size,
                                  uint64_t flags,
                                  uint32_t* out_page_count) {
    if (out_page_count != 0) {
        *out_page_count = 0;
    }
    if (space == 0) {
        return 0;
    }
    if (size == 0) {
        return 1;
    }

    if (!mutation_gate_enter(space)) {
        return 0;
    }
    KernelSpinlockToken token;
    if (!kernel_spinlock_acquire(&space->lock, &token)) {
        mutation_gate_release(space);
        return 0;
    }
    if (!ensure_root_unlocked(space)) {
        kernel_spinlock_release(&space->lock, &token);
        mutation_gate_release(space);
        return 0;
    }
    const uint64_t begin = align_down_page(virt);
    const uint64_t end = align_up_page(virt + size);
    uint32_t mapped = 0;
    for (uint64_t address = begin; address < end; address += VM_PAGE_SIZE) {
        const uint64_t phys = (uint64_t)(uintptr_t)pmm_alloc_block();
        if (phys == 0 ||
            !vm_map_page_in_root(space->root_phys, address, phys, flags)) {
            if (phys != 0) {
                pmm_free_block((void*)(uintptr_t)phys);
            }
            break;
        }
        mapped++;
    }
    const uint32_t expected = (uint32_t)((end - begin) / VM_PAGE_SIZE);
    AddressSpaceInvalidation invalidation = {};
    if (mapped != 0) {
        invalidation = begin_invalidation_unlocked(
            space,
            begin,
            mapped,
            mapped > ADDRESS_SPACE_TLB_PAGE_LIMIT);
    }
    kernel_spinlock_release(&space->lock, &token);

    if (mapped != 0 && !complete_invalidation(space, invalidation)) {
        return 0;
    }
    if (mapped == 0) {
        mutation_gate_release(space);
    }
    if (mapped != expected) {
        address_space_unmap_free_range(space, begin, mapped);
        return 0;
    }
    if (!address_space_add_region(space,
                                  virt,
                                  size,
                                  flags_to_rights(flags))) {
        address_space_unmap_free_range(space, begin, mapped);
        return 0;
    }
    if (out_page_count != 0) {
        *out_page_count = mapped;
    }
    return 1;
}

uint32_t address_space_unmap_free_range(AddressSpace* space,
                                        uint64_t virt,
                                        uint32_t page_count) {
    if (space == 0 || space->root_phys == 0 || page_count == 0) {
        return 0;
    }
    const uint64_t begin = align_down_page(virt);
    uint32_t total_unmapped = 0;
    while (total_unmapped < page_count) {
        uint32_t chunk = page_count - total_unmapped;
        if (chunk > ADDRESS_SPACE_QUARANTINE_LIMIT) {
            chunk = ADDRESS_SPACE_QUARANTINE_LIMIT;
        }
        uint64_t physical[ADDRESS_SPACE_QUARANTINE_LIMIT] = {};
        uint32_t unmapped = 0;
        const uint64_t chunk_begin =
            begin + (uint64_t)total_unmapped * VM_PAGE_SIZE;
        if (!mutation_gate_enter(space)) {
            break;
        }
        KernelSpinlockToken token;
        if (!kernel_spinlock_acquire(&space->lock, &token)) {
            mutation_gate_release(space);
            break;
        }
        for (; unmapped < chunk; unmapped++) {
            const uint64_t address =
                chunk_begin + (uint64_t)unmapped * VM_PAGE_SIZE;
            physical[unmapped] =
                vm_get_phys_in_root(space->root_phys, address) &
                ~(VM_PAGE_SIZE - 1ULL);
            if (physical[unmapped] == 0 ||
                !vm_unmap_page_in_root(space->root_phys, address)) {
                break;
            }
        }
        AddressSpaceInvalidation invalidation = {};
        if (unmapped != 0) {
            space->quarantined_page_count += unmapped;
            invalidation = begin_invalidation_unlocked(
                space,
                chunk_begin,
                unmapped,
                unmapped > ADDRESS_SPACE_TLB_PAGE_LIMIT);
        }
        kernel_spinlock_release(&space->lock, &token);
        if (unmapped == 0) {
            mutation_gate_release(space);
            break;
        }
        if (!complete_invalidation(space, invalidation)) {
            total_unmapped += unmapped;
            break;
        }
        for (uint32_t page = 0; page < unmapped; page++) {
            pmm_free_block((void*)(uintptr_t)physical[page]);
        }
        if (kernel_spinlock_acquire(&space->lock, &token)) {
            space->quarantined_page_count -= unmapped;
            space->retired_page_count += unmapped;
            kernel_spinlock_release(&space->lock, &token);
        }
        total_unmapped += unmapped;
        if (unmapped != chunk) {
            break;
        }
    }
    if (total_unmapped != 0) {
        address_space_remove_region(
            space,
            begin,
            (uint64_t)total_unmapped * VM_PAGE_SIZE);
    }
    return total_unmapped;
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

uint64_t address_space_identity(const AddressSpace* space) {
    return space != 0
        ? __atomic_load_n(&space->identity, __ATOMIC_ACQUIRE)
        : 0;
}

uint64_t address_space_tlb_generation(const AddressSpace* space) {
    return space != 0
        ? __atomic_load_n(&space->tlb_generation, __ATOMIC_ACQUIRE)
        : 0;
}
