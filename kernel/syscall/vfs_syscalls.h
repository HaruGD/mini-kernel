#ifndef KERNEL_SYSCALL_VFS_SYSCALLS_H
#define KERNEL_SYSCALL_VFS_SYSCALLS_H

#include <stdint.h>

int dispatch_vfs_handle_syscall64(uint64_t syscall_no,
                                  uint64_t arg1,
                                  uint64_t arg2,
                                  uint64_t arg3,
                                  uint64_t* result_out);

#endif
