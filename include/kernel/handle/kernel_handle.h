#ifndef KERNEL_HANDLE_KERNEL_HANDLE_H
#define KERNEL_HANDLE_KERNEL_HANDLE_H

#include <stdint.h>
#include "kernel/spinlock.h"
#include "os64/handle_types.h"

#define KERNEL_HANDLE_TABLE_SIZE 32u

#define KERNEL_HANDLE_TYPE_NONE OS_HANDLE_TYPE_NONE
#define KERNEL_HANDLE_TYPE_VFS_FILE OS_HANDLE_TYPE_VFS_FILE
#define KERNEL_HANDLE_TYPE_VFS_DIR OS_HANDLE_TYPE_VFS_DIR
#define KERNEL_HANDLE_TYPE_SHARED_MEMORY OS_HANDLE_TYPE_SHARED_MEMORY
#define KERNEL_HANDLE_TYPE_GRAPHICS_SURFACE OS_HANDLE_TYPE_GRAPHICS_SURFACE
#define KERNEL_HANDLE_TYPE_MUTEX OS_HANDLE_TYPE_MUTEX
#define KERNEL_HANDLE_TYPE_SEMAPHORE OS_HANDLE_TYPE_SEMAPHORE
#define KERNEL_HANDLE_TYPE_CONDITION OS_HANDLE_TYPE_CONDITION

#define KERNEL_HANDLE_RIGHT_READ OS_HANDLE_RIGHT_READ
#define KERNEL_HANDLE_RIGHT_WRITE OS_HANDLE_RIGHT_WRITE
#define KERNEL_HANDLE_RIGHT_SEEK OS_HANDLE_RIGHT_SEEK
#define KERNEL_HANDLE_RIGHT_ENUMERATE OS_HANDLE_RIGHT_ENUMERATE
#define KERNEL_HANDLE_RIGHT_MAP OS_HANDLE_RIGHT_MAP
#define KERNEL_HANDLE_RIGHT_TRANSFER OS_HANDLE_RIGHT_TRANSFER

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
    KernelSpinlock lock;
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
int kernel_handle_resolve_copy(const KernelHandleTable* table,
                               uint64_t handle,
                               uint32_t expected_type,
                               uint32_t required_rights,
                               KernelHandle* resolved_out);
int64_t kernel_handle_resolve_copy_result(const KernelHandleTable* table,
                                          uint64_t handle,
                                          uint32_t expected_type,
                                          uint32_t required_rights,
                                          KernelHandle* resolved_out);
int kernel_handle_restrict_rights(KernelHandleTable* table,
                                  uint64_t handle,
                                  uint32_t rights);
int kernel_handle_close(KernelHandleTable* table, uint64_t handle, KernelHandle* closed_out);
uint32_t kernel_handle_detach_all(KernelHandleTable* table,
                                  KernelHandle* detached,
                                  uint32_t capacity);
uint32_t kernel_handle_close_all_type(KernelHandleTable* table, uint32_t type);
uint32_t kernel_handle_count_type(const KernelHandleTable* table, uint32_t type);
int kernel_handle_is_valid_token(uint64_t handle);

#endif
