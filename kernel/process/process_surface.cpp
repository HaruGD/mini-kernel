#include "kernel/process_surface.h"

#include "kernel/graphics/surface_backing.h"
#include "kernel/handle/kernel_objects.h"
#include "kernel/syscall64.h"
#include "os64/surface_types.h"

static const uint64_t SURFACE_MAPPING_SLOT_SIZE =
    (uint64_t)KERNEL_GRAPHICS_SURFACE_MAX_PAGES * VM_PAGE_SIZE;
static_assert((uint64_t)PROCESS_SURFACE_MAPPING_MAX * SURFACE_MAPPING_SLOT_SIZE <=
                  VM_USER_SURFACE_LIMIT - VM_USER_SURFACE_BASE,
              "user surface arena is too small");

static uint64_t mapping_address(uint32_t slot) {
    return VM_USER_SURFACE_BASE + (uint64_t)slot * SURFACE_MAPPING_SLOT_SIZE;
}

static uint64_t mapping_vm_flags(uint32_t map_flags) {
    uint64_t flags = VM_FLAG_USER | VM_FLAG_NO_EXECUTE;
    if (map_flags & OS_SURFACE_MAP_WRITE) {
        flags |= VM_FLAG_WRITABLE;
    }
    return flags;
}

static uint32_t mapping_region_rights(uint32_t map_flags) {
    uint32_t rights = ADDRESS_SPACE_REGION_READ;
    if (map_flags & OS_SURFACE_MAP_WRITE) {
        rights |= ADDRESS_SPACE_REGION_WRITE;
    }
    return rights;
}

void process_surface_mappings_reset(Process* process) {
    if (process == 0) {
        return;
    }
    process->next_surface_mapping_generation = 1;
    process->active_surface_mapping_count = 0;
    for (uint32_t i = 0; i < PROCESS_SURFACE_MAPPING_MAX; i++) {
        ProcessSurfaceMapping* mapping = &process->surface_mappings[i];
        mapping->active = 0;
        mapping->reserved0 = 0;
        mapping->reserved1 = 0;
        mapping->map_flags = 0;
        mapping->page_count = 0;
        mapping->mapping_generation = 0;
        mapping->object_id = 0;
        mapping->user_address = 0;
        mapping->byte_size = 0;
    }
}

uint64_t process_surface_map(Process* process, uint64_t handle, uint32_t map_flags) {
    if (process == 0 || map_flags == 0 ||
        (map_flags & ~OS_SURFACE_MAP_VALID_MASK) != 0) {
        return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
    }

    uint32_t required_rights = KERNEL_HANDLE_RIGHT_MAP;
    if (map_flags & OS_SURFACE_MAP_READ) {
        required_rights |= KERNEL_HANDLE_RIGHT_READ;
    }
    if (map_flags & OS_SURFACE_MAP_WRITE) {
        required_rights |= KERNEL_HANDLE_RIGHT_WRITE;
    }
    KernelHandle resolved;
    if (!kernel_handle_resolve_copy(&process->handle_table,
                                    handle,
                                    KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE,
                                    required_rights,
                                    &resolved)) {
        return (uint64_t)(int64_t)SYS_ERR_PERMISSION_DENIED;
    }

    for (uint32_t i = 0; i < PROCESS_SURFACE_MAPPING_MAX; i++) {
        const ProcessSurfaceMapping* mapping = &process->surface_mappings[i];
        if (mapping->active && mapping->object_id == resolved.object) {
            return mapping->map_flags == map_flags
                ? mapping->user_address
                : (uint64_t)(int64_t)SYS_ERR_ALREADY_EXISTS;
        }
    }

    uint32_t slot = PROCESS_SURFACE_MAPPING_MAX;
    for (uint32_t i = 0; i < PROCESS_SURFACE_MAPPING_MAX; i++) {
        if (!process->surface_mappings[i].active) {
            slot = i;
            break;
        }
    }
    if (slot == PROCESS_SURFACE_MAPPING_MAX) {
        return (uint64_t)(int64_t)SYS_ERR_NO_RESOURCES;
    }

    uint32_t backing_slot = 0;
    uint32_t page_count = 0;
    uint32_t byte_size = 0;
    if (kernel_graphics_surface_get_backing(resolved.object,
                                            &backing_slot,
                                            &page_count,
                                            &byte_size) != KERNEL_OBJECT_OK ||
        page_count == 0 || page_count > KERNEL_GRAPHICS_SURFACE_MAX_PAGES) {
        return (uint64_t)(int64_t)SYS_ERR_NOT_FOUND;
    }

    uint64_t user_address = mapping_address(slot);
    uint64_t mapped_size = (uint64_t)page_count * VM_PAGE_SIZE;
    if (user_address < VM_USER_SURFACE_BASE ||
        user_address + mapped_size > VM_USER_SURFACE_LIMIT) {
        return (uint64_t)(int64_t)SYS_ERR_OUT_OF_RANGE;
    }
    if (!address_space_add_region(&process->address_space,
                                  user_address,
                                  mapped_size,
                                  mapping_region_rights(map_flags))) {
        return (uint64_t)(int64_t)SYS_ERR_NO_RESOURCES;
    }

    uint64_t vm_flags = mapping_vm_flags(map_flags);
    uint32_t mapped_pages = 0;
    for (; mapped_pages < page_count; mapped_pages++) {
        uint64_t phys = kernel_graphics_surface_backing_get_phys(backing_slot, mapped_pages);
        if (phys == 0 ||
            !address_space_map_page(&process->address_space,
                                    user_address + (uint64_t)mapped_pages * VM_PAGE_SIZE,
                                    phys,
                                    vm_flags)) {
            break;
        }
    }
    if (mapped_pages != page_count) {
        while (mapped_pages != 0) {
            mapped_pages--;
            address_space_unmap_page(&process->address_space,
                                     user_address + (uint64_t)mapped_pages * VM_PAGE_SIZE);
        }
        address_space_remove_region(&process->address_space, user_address, mapped_size);
        return (uint64_t)(int64_t)SYS_ERR_OUT_OF_MEMORY;
    }

    ProcessSurfaceMapping* mapping = &process->surface_mappings[slot];
    uint32_t generation = process->next_surface_mapping_generation++;
    if (process->next_surface_mapping_generation == 0) {
        process->next_surface_mapping_generation = 1;
    }
    mapping->active = 1;
    mapping->map_flags = map_flags;
    mapping->page_count = page_count;
    mapping->mapping_generation = generation == 0 ? 1 : generation;
    mapping->object_id = resolved.object;
    mapping->user_address = user_address;
    mapping->byte_size = byte_size;
    process->active_surface_mapping_count++;
    return user_address;
}

static int unmap_slot(Process* process, uint32_t slot) {
    ProcessSurfaceMapping* mapping = &process->surface_mappings[slot];
    if (!mapping->active) {
        return SYS_ERR_NOT_FOUND;
    }
    uint32_t unmapped = 0;
    for (; unmapped < mapping->page_count; unmapped++) {
        if (!address_space_unmap_page(&process->address_space,
                                      mapping->user_address +
                                          (uint64_t)unmapped * VM_PAGE_SIZE)) {
            break;
        }
    }
    if (unmapped != mapping->page_count) {
        uint32_t backing_slot = 0;
        uint32_t page_count = 0;
        uint32_t byte_size = 0;
        if (kernel_graphics_surface_get_backing(mapping->object_id,
                                                &backing_slot,
                                                &page_count,
                                                &byte_size) == KERNEL_OBJECT_OK) {
            for (uint32_t page = 0; page < unmapped; page++) {
                uint64_t phys = kernel_graphics_surface_backing_get_phys(backing_slot, page);
                address_space_map_page(&process->address_space,
                                       mapping->user_address + (uint64_t)page * VM_PAGE_SIZE,
                                       phys,
                                       mapping_vm_flags(mapping->map_flags));
            }
        }
        return SYS_ERR_IO;
    }

    address_space_remove_region(&process->address_space,
                                mapping->user_address,
                                (uint64_t)mapping->page_count * VM_PAGE_SIZE);
    mapping->active = 0;
    mapping->map_flags = 0;
    mapping->page_count = 0;
    mapping->mapping_generation = 0;
    mapping->object_id = 0;
    mapping->user_address = 0;
    mapping->byte_size = 0;
    if (process->active_surface_mapping_count != 0) {
        process->active_surface_mapping_count--;
    }
    return 0;
}

int process_surface_unmap(Process* process, uint64_t handle, uint64_t user_address) {
    if (process == 0 || user_address == 0) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    KernelHandle resolved;
    if (!kernel_handle_resolve_copy(&process->handle_table,
                                    handle,
                                    KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE,
                                    KERNEL_HANDLE_RIGHT_MAP,
                                    &resolved)) {
        return SYS_ERR_PERMISSION_DENIED;
    }
    for (uint32_t i = 0; i < PROCESS_SURFACE_MAPPING_MAX; i++) {
        ProcessSurfaceMapping* mapping = &process->surface_mappings[i];
        if (mapping->active && mapping->object_id == resolved.object) {
            if (mapping->user_address != user_address) {
                return SYS_ERR_INVALID_ARGUMENT;
            }
            return unmap_slot(process, i);
        }
    }
    return SYS_ERR_NOT_FOUND;
}

int process_surface_unmap_object(Process* process, uint64_t object_id) {
    if (process == 0 || object_id == 0) {
        return SYS_ERR_INVALID_ARGUMENT;
    }
    for (uint32_t i = 0; i < PROCESS_SURFACE_MAPPING_MAX; i++) {
        if (process->surface_mappings[i].active &&
            process->surface_mappings[i].object_id == object_id) {
            return unmap_slot(process, i);
        }
    }
    return 0;
}

uint32_t process_surface_unmap_all(Process* process) {
    if (process == 0) {
        return 0;
    }
    uint32_t failures = 0;
    for (uint32_t i = 0; i < PROCESS_SURFACE_MAPPING_MAX; i++) {
        if (!process->surface_mappings[i].active) {
            continue;
        }
        int result = SYS_ERR_IO;
        for (uint32_t attempt = 0; attempt < 3 && result != 0; attempt++) {
            result = unmap_slot(process, i);
        }
        if (result != 0) {
            failures++;
        }
    }
    return failures;
}
