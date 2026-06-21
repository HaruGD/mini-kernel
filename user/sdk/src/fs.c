#include <os64/os64.h>
#include "internal.h"

long os_open(const char* path, uint32_t mode) {
    return os_syscall2(OS_SYS_OPEN, (long)path, mode);
}

long os_read(long fd, void* buffer, uint32_t size) {
    return os_syscall3(OS_SYS_READ, fd, (long)buffer, size);
}

long os_write_fd(long fd, const void* buffer, uint32_t size) {
    return os_syscall3(OS_SYS_WRITE_FD, fd, (long)buffer, size);
}

long os_close(long fd) {
    return os_syscall1(OS_SYS_CLOSE, fd);
}

long os_seek(long fd, int32_t offset, uint32_t whence) {
    return os_syscall3(OS_SYS_SEEK, fd, offset, whence);
}

long os_tell(long fd) {
    return os_syscall1(OS_SYS_TELL, fd);
}

long os_stat(const char* path, OsFileInfo* info) {
    return os_syscall2(OS_SYS_STAT, (long)path, (long)info);
}

long os_getcwd(char* buffer, uint32_t capacity) {
    return os_syscall2(OS_SYS_GETCWD, (long)buffer, capacity);
}

long os_chdir(const char* path) {
    return os_syscall1(OS_SYS_CHDIR, (long)path);
}

long os_mkdir(const char* path) {
    return os_syscall1(OS_SYS_MKDIR, (long)path);
}

long os_rmdir(const char* path) {
    return os_syscall1(OS_SYS_RMDIR, (long)path);
}

long os_remove(const char* path) {
    return os_syscall1(OS_SYS_REMOVE, (long)path);
}

long os_rename(const char* old_path, const char* new_path) {
    return os_syscall2(OS_SYS_RENAME, (long)old_path, (long)new_path);
}

long os_touch(const char* path) {
    return os_syscall1(OS_SYS_TOUCH, (long)path);
}

long os_opendir(const char* path) {
    return os_syscall1(OS_SYS_OPENDIR, (long)path);
}

long os_readdir(long fd, OsDirEntry* entry) {
    return os_syscall2(OS_SYS_READDIR, fd, (long)entry);
}

long os_closedir(long fd) {
    return os_syscall1(OS_SYS_CLOSEDIR, fd);
}

static long transfer(long fd, void* buffer, uint32_t size, int writing) {
    uint32_t total = 0;

    while (total < size) {
        uint32_t chunk = size - total;
        long result;

        if (chunk > 4096u) {
            chunk = 4096u;
        }
        if (writing != 0) {
            result = os_write_fd(fd, (const uint8_t*)buffer + total, chunk);
        } else {
            result = os_read(fd, (uint8_t*)buffer + total, chunk);
        }
        if (result < 0) {
            return result;
        }
        if (result == 0) {
            break;
        }
        total += (uint32_t)result;
        if (!writing && (uint32_t)result < chunk) {
            break;
        }
    }
    return (long)total;
}

long os_read_file(const char* path, void* buffer, uint32_t capacity) {
    long fd;
    long result;

    if (path == 0 || (buffer == 0 && capacity != 0)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    fd = os_open(path, OS_OPEN_READ);
    if (fd < 0) {
        return fd;
    }
    result = transfer(fd, buffer, capacity, 0);
    long close_result = os_close(fd);
    if (result >= 0 && close_result < 0) {
        return close_result;
    }
    return result;
}

long os_read_text_file(const char* path, char* buffer, uint32_t capacity) {
    long result;

    if (buffer == 0 || capacity == 0) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    result = os_read_file(path, buffer, capacity - 1u);
    if (result < 0) {
        buffer[0] = '\0';
        return result;
    }
    buffer[result] = '\0';
    return result;
}

static long write_file(const char* path, const void* data, uint32_t size, uint32_t mode) {
    long fd;
    long result;

    if (path == 0 || (data == 0 && size != 0)) {
        return OS_ERR_INVALID_ARGUMENT;
    }
    fd = os_open(path, mode);
    if (fd < 0) {
        return fd;
    }
    result = transfer(fd, (void*)data, size, 1);
    long close_result = os_close(fd);
    if (result < 0) {
        return result;
    }
    if (close_result < 0) {
        return close_result;
    }
    if (result != (long)size) {
        return OS_ERR_IO;
    }
    return result;
}

long os_write_file(const char* path, const void* data, uint32_t size) {
    return write_file(path, data, size, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_TRUNCATE);
}

long os_append_file(const char* path, const void* data, uint32_t size) {
    return write_file(path, data, size, OS_OPEN_WRITE | OS_OPEN_CREATE | OS_OPEN_APPEND);
}

void* os_read_file_alloc(const char* path, uint32_t* size_out) {
    OsFileInfo info;
    if (size_out != 0) {
        *size_out = 0;
    }
    if (os_stat(path, &info) < 0 || info.type != OS_NODE_FILE) {
        return 0;
    }

    uint32_t allocation_size = info.size != 0 ? info.size : 1u;
    void* buffer = os_malloc(allocation_size);
    if (buffer == 0) {
        return 0;
    }

    long bytes_read = os_read_file(path, buffer, info.size);
    if (bytes_read < 0 || (uint32_t)bytes_read != info.size) {
        os_free(buffer);
        return 0;
    }
    if (size_out != 0) {
        *size_out = info.size;
    }
    return buffer;
}

char* os_read_text_file_alloc(const char* path, uint32_t* size_out) {
    OsFileInfo info;
    if (size_out != 0) {
        *size_out = 0;
    }
    if (os_stat(path, &info) < 0 || info.type != OS_NODE_FILE || info.size == UINT32_MAX) {
        return 0;
    }

    char* buffer = (char*)os_malloc((size_t)info.size + 1u);
    if (buffer == 0) {
        return 0;
    }

    long bytes_read = os_read_file(path, buffer, info.size);
    if (bytes_read < 0 || (uint32_t)bytes_read != info.size) {
        os_free(buffer);
        return 0;
    }
    buffer[info.size] = '\0';
    if (size_out != 0) {
        *size_out = info.size;
    }
    return buffer;
}
