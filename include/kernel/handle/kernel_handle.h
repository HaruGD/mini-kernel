#ifndef KERNEL_HANDLE_KERNEL_HANDLE_H
#define KERNEL_HANDLE_KERNEL_HANDLE_H

#include <stdint.h>

#define KERNEL_HANDLE_TABLE_SIZE 32u

#define KERNEL_HANDLE_TYPE_NONE 0u
#define KERNEL_HANDLE_TYPE_VFS_FILE 1u
#define KERNEL_HANDLE_TYPE_VFS_DIR 2u
#define KERNEL_HANDLE_TYPE_SHARED_MEMORY 3u
#define KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE 4u

#define KERNEL_HANDLE_RIGHT_READ 0x00000001u
#define KERNEL_HANDLE_RIGHT_WRITE 0x00000002u
#define KERNEL_HANDLE_RIGHT_SEEK 0x00000004u
#define KERNEL_HANDLE_RIGHT_ENUMERATE 0x00000008u
#define KERNEL_HANDLE_RIGHT_MAP 0x00000010u
#define KERNEL_HANDLE_RIGHT_TRANSFER 0x00000020u

struct KernelHandle {
    uint8_t active;
    uint8_t type;
    uint16_t reserved0;
    uint32_t rights;
    uint32_t generation;
    uint32_t reserved1;
    uint64_t object;
    uint64_t extra;
};

struct KernelHandleTable {
    uint32_t next_generation;
    uint32_t active_count;
    KernelHandle entries[KERNEL_HANDLE_TABLE_SIZE];
};

void kernel_handle_table_init(KernelHandleTable* table);
uint64_t kernel_handle_alloc(KernelHandleTable* table,
                             uint32_t type,
                             uint32_t rights,
                             uint64_t object,
                             uint64_t extra);
KernelHandle* kernel_handle_resolve(KernelHandleTable* table,
                                    uint64_t handle,
                                    uint32_t expected_type,
                                    uint32_t required_rights);
const KernelHandle* kernel_handle_resolve_const(const KernelHandleTable* table,
                                                uint64_t handle,
                                                uint32_t expected_type,
                                                uint32_t required_rights);
int kernel_handle_close(KernelHandleTable* table, uint64_t handle, KernelHandle* closed_out);
uint32_t kernel_handle_close_all_type(KernelHandleTable* table, uint32_t type);
uint32_t kernel_handle_count_type(const KernelHandleTable* table, uint32_t type);
int kernel_handle_is_valid_token(uint64_t handle);

#endif
