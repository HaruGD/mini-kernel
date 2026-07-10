#include "kernel/handle/kernel_handle.h"

static uint64_t encode_handle_token(uint32_t slot, uint32_t generation) {
    return ((uint64_t)generation << 8) | (uint64_t)(slot + 1u);
}

static int decode_handle_token(uint64_t handle, uint32_t* slot_out, uint32_t* generation_out) {
    if ((handle & 0xFFu) == 0 || handle > 0xFFFFFFFFULL) {
        return 0;
    }

    uint32_t slot = (uint32_t)(handle & 0xFFu) - 1u;
    if (slot >= KERNEL_HANDLE_TABLE_SIZE) {
        return 0;
    }

    uint32_t generation = (uint32_t)(handle >> 8);
    if (generation == 0) {
        return 0;
    }

    *slot_out = slot;
    *generation_out = generation;
    return 1;
}

static uint32_t next_generation(KernelHandleTable* table) {
    uint32_t generation = table->next_generation++;
    if (table->next_generation == 0 || table->next_generation > 0x00FFFFFFu) {
        table->next_generation = 1;
    }
    return generation == 0 ? next_generation(table) : generation;
}

void kernel_handle_table_init(KernelHandleTable* table) {
    if (table == 0) {
        return;
    }

    table->next_generation = 1;
    table->active_count = 0;
    for (uint32_t i = 0; i < KERNEL_HANDLE_TABLE_SIZE; i++) {
        table->entries[i].active = 0;
        table->entries[i].type = KERNEL_HANDLE_TYPE_NONE;
        table->entries[i].reserved0 = 0;
        table->entries[i].rights = 0;
        table->entries[i].generation = 0;
        table->entries[i].reserved1 = 0;
        table->entries[i].object = 0;
        table->entries[i].extra = 0;
    }
}

uint64_t kernel_handle_alloc(KernelHandleTable* table,
                             uint32_t type,
                             uint32_t rights,
                             uint64_t object,
                             uint64_t extra) {
    if (table == 0 || type == KERNEL_HANDLE_TYPE_NONE) {
        return 0;
    }

    for (uint32_t i = 0; i < KERNEL_HANDLE_TABLE_SIZE; i++) {
        KernelHandle* entry = &table->entries[i];
        if (entry->active) {
            continue;
        }

        uint32_t generation = next_generation(table);
        entry->active = 1;
        entry->type = (uint8_t)type;
        entry->reserved0 = 0;
        entry->rights = rights;
        entry->generation = generation;
        entry->reserved1 = 0;
        entry->object = object;
        entry->extra = extra;
        table->active_count++;
        return encode_handle_token(i, generation);
    }

    return 0;
}

KernelHandle* kernel_handle_resolve(KernelHandleTable* table,
                                    uint64_t handle,
                                    uint32_t expected_type,
                                    uint32_t required_rights) {
    if (table == 0) {
        return 0;
    }

    uint32_t slot = 0;
    uint32_t generation = 0;
    if (!decode_handle_token(handle, &slot, &generation)) {
        return 0;
    }

    KernelHandle* entry = &table->entries[slot];
    if (!entry->active || entry->generation != generation) {
        return 0;
    }
    if (expected_type != KERNEL_HANDLE_TYPE_NONE && entry->type != expected_type) {
        return 0;
    }
    if ((entry->rights & required_rights) != required_rights) {
        return 0;
    }
    return entry;
}

const KernelHandle* kernel_handle_resolve_const(const KernelHandleTable* table,
                                                uint64_t handle,
                                                uint32_t expected_type,
                                                uint32_t required_rights) {
    return kernel_handle_resolve((KernelHandleTable*)table, handle, expected_type, required_rights);
}

int kernel_handle_close(KernelHandleTable* table, uint64_t handle, KernelHandle* closed_out) {
    KernelHandle* entry = kernel_handle_resolve(table, handle, KERNEL_HANDLE_TYPE_NONE, 0);
    if (entry == 0) {
        return 0;
    }

    if (closed_out != 0) {
        *closed_out = *entry;
    }

    entry->active = 0;
    entry->type = KERNEL_HANDLE_TYPE_NONE;
    entry->rights = 0;
    entry->object = 0;
    entry->extra = 0;
    if (table->active_count != 0) {
        table->active_count--;
    }
    return 1;
}

uint32_t kernel_handle_close_all_type(KernelHandleTable* table, uint32_t type) {
    if (table == 0) {
        return 0;
    }

    uint32_t closed = 0;
    for (uint32_t i = 0; i < KERNEL_HANDLE_TABLE_SIZE; i++) {
        KernelHandle* entry = &table->entries[i];
        if (!entry->active || (type != KERNEL_HANDLE_TYPE_NONE && entry->type != type)) {
            continue;
        }

        entry->active = 0;
        entry->type = KERNEL_HANDLE_TYPE_NONE;
        entry->rights = 0;
        entry->object = 0;
        entry->extra = 0;
        closed++;
    }

    table->active_count = table->active_count > closed ? table->active_count - closed : 0;
    return closed;
}

uint32_t kernel_handle_count_type(const KernelHandleTable* table, uint32_t type) {
    if (table == 0) {
        return 0;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < KERNEL_HANDLE_TABLE_SIZE; i++) {
        const KernelHandle* entry = &table->entries[i];
        if (entry->active && (type == KERNEL_HANDLE_TYPE_NONE || entry->type == type)) {
            count++;
        }
    }
    return count;
}

int kernel_handle_is_valid_token(uint64_t handle) {
    uint32_t slot = 0;
    uint32_t generation = 0;
    return decode_handle_token(handle, &slot, &generation);
}
