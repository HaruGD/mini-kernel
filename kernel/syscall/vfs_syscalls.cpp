#include "kernel/syscall/vfs_syscalls.h"

#include <stddef.h>
#include <stdint.h>

extern "C" {
    #include "kernel/mm/heap.h"
}

#include "fs/vfs.h"
#include "kernel/handle/kernel_handle.h"
#include "kernel/kutil64.h"
#include "kernel/process.h"
#include "kernel/process64.h"
#include "kernel/syscall64.h"
#include "kernel/userprog64.h"

#define VFS_USER_PATH_MAX PROCESS_CMDLINE_MAX

static uint32_t vfs_mode_to_handle_rights(uint32_t mode) {
    uint32_t rights = KERNEL_HANDLE_RIGHT_SEEK;
    if ((mode & VFS_OPEN_READ) != 0) {
        rights |= KERNEL_HANDLE_RIGHT_READ;
    }
    if ((mode & VFS_OPEN_WRITE) != 0) {
        rights |= KERNEL_HANDLE_RIGHT_WRITE;
    }
    return rights;
}

static KernelHandle* resolve_current_handle(uint64_t handle, uint32_t type, uint32_t rights) {
    Process* process = current_process();
    if (process == 0) {
        return 0;
    }
    return kernel_handle_resolve(&process->handle_table, handle, type, rights);
}

static int resolve_vfs_fd(uint64_t handle, uint32_t type, uint32_t rights, int* fd_out) {
    KernelHandle* entry = resolve_current_handle(handle, type, rights);
    if (entry == 0 || fd_out == 0 || entry->object > 0x7FFFFFFFULL) {
        return 0;
    }
    *fd_out = (int)entry->object;
    return 1;
}

static uint64_t allocate_current_handle(uint32_t type,
                                        uint32_t rights,
                                        uint64_t object,
                                        uint64_t extra) {
    Process* process = current_process();
    if (process == 0) {
        return 0;
    }
    return kernel_handle_alloc(&process->handle_table, type, rights, object, extra);
}

static uint64_t dispatch_open(uint64_t path_address, uint64_t mode_arg) {
    char file_name[VFS_USER_PATH_MAX];
    if (!copy_user_cstring((const char*)(uintptr_t)path_address, file_name, sizeof(file_name))) {
        print("\nInvalid user filename pointer.");
        return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
    }

    Process* owner = current_process();
    uint32_t mode = (uint32_t)mode_arg;
    int fd = vfs_open_for_owner(file_name, mode, owner != 0 ? owner->pid : 0);
    if (fd < 0) {
        return (uint64_t)(int64_t)fd;
    }

    uint64_t handle = allocate_current_handle(KERNEL_HANDLE_TYPE_VFS_FILE,
                                              vfs_mode_to_handle_rights(mode),
                                              (uint64_t)(uint32_t)fd,
                                              0);
    if (handle == 0) {
        vfs_close(fd);
        return (uint64_t)(int64_t)SYS_ERR_NO_RESOURCES;
    }
    return handle;
}

static uint64_t dispatch_read(uint64_t handle, uint64_t buffer_address, uint64_t size_arg) {
    uint32_t requested = (uint32_t)size_arg;
    if (requested == 0) {
        return 0;
    }
    if (requested > 4096) {
        requested = 4096;
    }

    uint8_t* temp = (uint8_t*)kmalloc(requested);
    if (temp == 0) {
        return (uint64_t)(int64_t)SYS_ERR_OUT_OF_MEMORY;
    }

    int fd = -1;
    if (!resolve_vfs_fd(handle, KERNEL_HANDLE_TYPE_VFS_FILE, KERNEL_HANDLE_RIGHT_READ, &fd)) {
        kfree(temp);
        return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
    }

    uint32_t bytes_read = 0;
    int result = vfs_read(fd, temp, requested, &bytes_read);
    if (result != VFS_OK) {
        kfree(temp);
        return (uint64_t)(int64_t)result;
    }
    if (!copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)buffer_address, temp, bytes_read)) {
        kfree(temp);
        return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
    }

    kfree(temp);
    return bytes_read;
}

static uint64_t dispatch_write(uint64_t handle, uint64_t buffer_address, uint64_t size_arg) {
    uint32_t requested = (uint32_t)size_arg;
    if (requested == 0) {
        return 0;
    }
    if (requested > 4096) {
        requested = 4096;
    }

    uint8_t* temp = (uint8_t*)kmalloc(requested);
    if (temp == 0) {
        return (uint64_t)(int64_t)SYS_ERR_OUT_OF_MEMORY;
    }
    if (!copy_user_buffer((const uint8_t*)(uintptr_t)buffer_address, temp, requested)) {
        kfree(temp);
        return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
    }

    int fd = -1;
    if (!resolve_vfs_fd(handle, KERNEL_HANDLE_TYPE_VFS_FILE, KERNEL_HANDLE_RIGHT_WRITE, &fd)) {
        kfree(temp);
        return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
    }

    uint32_t bytes_written = 0;
    int result = vfs_write(fd, temp, requested, &bytes_written);
    kfree(temp);
    return result == VFS_OK ? bytes_written : (uint64_t)(int64_t)result;
}

static uint64_t dispatch_close_typed(uint64_t handle, uint32_t type) {
    Process* process = current_process();
    KernelHandle closed;
    if (process == 0 ||
        kernel_handle_resolve(&process->handle_table, handle, type, 0) == 0 ||
        !kernel_handle_close(&process->handle_table, handle, &closed) ||
        closed.object > 0x7FFFFFFFULL) {
        return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
    }
    return type == KERNEL_HANDLE_TYPE_VFS_DIR
        ? (uint64_t)(int64_t)vfs_closedir((int)closed.object)
        : (uint64_t)(int64_t)vfs_close((int)closed.object);
}

static uint64_t dispatch_seek(uint64_t handle, uint64_t offset_arg, uint64_t whence_arg) {
    int fd = -1;
    if (!resolve_vfs_fd(handle, KERNEL_HANDLE_TYPE_VFS_FILE, KERNEL_HANDLE_RIGHT_SEEK, &fd)) {
        return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
    }

    uint32_t position = 0;
    int result = vfs_seek(fd, (int32_t)offset_arg, (uint32_t)whence_arg, &position);
    return result == VFS_OK ? position : (uint64_t)(int64_t)result;
}

static uint64_t dispatch_tell(uint64_t handle) {
    int fd = -1;
    if (!resolve_vfs_fd(handle, KERNEL_HANDLE_TYPE_VFS_FILE, KERNEL_HANDLE_RIGHT_SEEK, &fd)) {
        return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
    }

    uint32_t position = 0;
    int result = vfs_tell(fd, &position);
    return result == VFS_OK ? position : (uint64_t)(int64_t)result;
}

static uint64_t dispatch_opendir(uint64_t path_address) {
    char dir_path[VFS_USER_PATH_MAX];
    if (!copy_user_cstring((const char*)(uintptr_t)path_address, dir_path, sizeof(dir_path))) {
        return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
    }

    Process* owner = current_process();
    int fd = vfs_opendir_for_owner(dir_path, owner != 0 ? owner->pid : 0);
    if (fd < 0) {
        return (uint64_t)(int64_t)fd;
    }

    uint64_t handle = allocate_current_handle(KERNEL_HANDLE_TYPE_VFS_DIR,
                                              KERNEL_HANDLE_RIGHT_ENUMERATE,
                                              (uint64_t)(uint32_t)fd,
                                              0);
    if (handle == 0) {
        vfs_closedir(fd);
        return (uint64_t)(int64_t)SYS_ERR_NO_RESOURCES;
    }
    return handle;
}

static uint64_t dispatch_readdir(uint64_t handle, uint64_t entry_address) {
    int fd = -1;
    if (!resolve_vfs_fd(handle, KERNEL_HANDLE_TYPE_VFS_DIR, KERNEL_HANDLE_RIGHT_ENUMERATE, &fd)) {
        return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
    }

    VFSDirEntry entry;
    int result = vfs_readdir(fd, &entry);
    if (result <= 0) {
        return (uint64_t)result;
    }
    if (entry_address == 0) {
        return (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
    }
    return copy_kernel_to_user_buffer((uint8_t*)(uintptr_t)entry_address,
                                      (const uint8_t*)&entry,
                                      sizeof(entry))
        ? 1
        : (uint64_t)(int64_t)SYS_ERR_INVALID_ARGUMENT;
}

int dispatch_vfs_handle_syscall64(uint64_t syscall_no,
                                  uint64_t arg1,
                                  uint64_t arg2,
                                  uint64_t arg3,
                                  uint64_t* result_out) {
    if (result_out == 0) {
        return 0;
    }

    if (syscall_no == SYS_VFS_OPEN) {
        *result_out = dispatch_open(arg1, arg2);
        return 1;
    }
    if (syscall_no == SYS_VFS_READ) {
        *result_out = dispatch_read(arg1, arg2, arg3);
        return 1;
    }
    if (syscall_no == SYS_VFS_WRITE) {
        *result_out = dispatch_write(arg1, arg2, arg3);
        return 1;
    }
    if (syscall_no == SYS_VFS_CLOSE) {
        *result_out = dispatch_close_typed(arg1, KERNEL_HANDLE_TYPE_VFS_FILE);
        return 1;
    }
    if (syscall_no == SYS_VFS_SEEK) {
        *result_out = dispatch_seek(arg1, arg2, arg3);
        return 1;
    }
    if (syscall_no == SYS_VFS_TELL) {
        *result_out = dispatch_tell(arg1);
        return 1;
    }
    if (syscall_no == SYS_VFS_OPENDIR) {
        *result_out = dispatch_opendir(arg1);
        return 1;
    }
    if (syscall_no == SYS_VFS_READDIR) {
        *result_out = dispatch_readdir(arg1, arg2);
        return 1;
    }
    if (syscall_no == SYS_VFS_CLOSEDIR) {
        *result_out = dispatch_close_typed(arg1, KERNEL_HANDLE_TYPE_VFS_DIR);
        return 1;
    }
    return 0;
}
