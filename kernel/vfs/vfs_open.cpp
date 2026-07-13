int vfs_open_for_owner(const char* path, uint32_t mode, uint32_t owner_pid) {
    if (path == 0 || path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    if ((mode & (VFS_OPEN_READ | VFS_OPEN_WRITE)) == 0) {
        return VFS_ERR_INVALID_PATH;
    }

    VFSFileInfo info;
    int info_result = vfs_get_file_info(path, &info);
    int exists = info_result == VFS_OK;
    if (!exists && !(mode & VFS_OPEN_CREATE)) {
        return info_result;
    }
    if (exists && info.type != VFS_NODE_FILE) {
        return VFS_ERR_INVALID_PATH;
    }

    int fd = vfs_alloc_open_slot();
    if (fd < 0) {
        return VFS_ERR_NO_SLOT;
    }

    VFSOpenFile* file = &g_vfs_open_files[fd];

    uint32_t initial_size = 0;
    if (exists && !(mode & VFS_OPEN_TRUNCATE)) {
        initial_size = info.size;
    }

    uint32_t initial_capacity = vfs_growth_capacity(initial_size > 0 ? initial_size : 1);
    file->buffer = (uint8_t*)kmalloc(initial_capacity);
    if (file->buffer == 0) {
        return VFS_ERR_IO;
    }

    file->active = 1;
    file->dirty = (!exists && (mode & VFS_OPEN_CREATE)) || ((mode & VFS_OPEN_WRITE) && (mode & VFS_OPEN_TRUNCATE));
    file->kind = VFS_HANDLE_FILE;
    file->mode = (uint16_t)mode;
    file->owner_pid = owner_pid;
    file->position = 0;
    file->size = initial_size;
    file->capacity = initial_capacity;
    file->dir_cursor = 0;
    vfs_copy_string(file->path, sizeof(file->path), path);

    if (initial_size > 0) {
        uint32_t bytes_read = 0;
        if (vfs_read_file(path, file->buffer, file->capacity, &bytes_read) != VFS_OK) {
            vfs_reset_open_file(file);
            return VFS_ERR_IO;
        }
        file->size = bytes_read;
    }

    if (mode & VFS_OPEN_APPEND) {
        file->position = file->size;
    }

    return fd;
}

int vfs_open(const char* path, uint32_t mode) {
    return vfs_open_for_owner(path, mode, 0);
}

int vfs_read(int fd, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read_out) {
    VFSOpenFile* file = vfs_get_open_file(fd);
    if (file == 0 || file->kind != VFS_HANDLE_FILE || buffer == 0) {
        return VFS_ERR_INVALID_PATH;
    }
    if ((file->mode & VFS_OPEN_READ) == 0) {
        return VFS_ERR_UNSUPPORTED;
    }

    uint32_t available = file->position < file->size ? (file->size - file->position) : 0;
    uint32_t count = buffer_size < available ? buffer_size : available;
    if (count > 0) {
        vfs_mem_copy(buffer, file->buffer + file->position, count);
        file->position += count;
    }
    if (bytes_read_out != 0) {
        *bytes_read_out = count;
    }
    return VFS_OK;
}

int vfs_write(int fd, const uint8_t* buffer, uint32_t size, uint32_t* bytes_written_out) {
    VFSOpenFile* file = vfs_get_open_file(fd);
    if (file == 0 || file->kind != VFS_HANDLE_FILE || (size > 0 && buffer == 0)) {
        return VFS_ERR_INVALID_PATH;
    }
    if ((file->mode & VFS_OPEN_WRITE) == 0) {
        return VFS_ERR_UNSUPPORTED;
    }

    uint32_t required = file->position + size;
    if (vfs_ensure_open_file_capacity(file, required > 0 ? required : 1) != VFS_OK) {
        return VFS_ERR_IO;
    }

    if (size > 0) {
        vfs_mem_copy(file->buffer + file->position, buffer, size);
        file->position += size;
        if (file->position > file->size) {
            file->size = file->position;
        }
    }
    file->dirty = 1;
    if (bytes_written_out != 0) {
        *bytes_written_out = size;
    }
    return VFS_OK;
}

int vfs_seek(int fd, int32_t offset, uint32_t whence, uint32_t* position_out) {
    VFSOpenFile* file = vfs_get_open_file(fd);
    if (file == 0 || file->kind != VFS_HANDLE_FILE) {
        return VFS_ERR_INVALID_PATH;
    }

    int64_t base = 0;
    switch (whence) {
        case VFS_SEEK_SET:
            base = 0;
            break;
        case VFS_SEEK_CUR:
            base = (int64_t)file->position;
            break;
        case VFS_SEEK_END:
            base = (int64_t)file->size;
            break;
        default:
            return VFS_ERR_INVALID_PATH;
    }

    int64_t next = base + (int64_t)offset;
    if (next < 0 || next > 0x7FFFFFFFLL) {
        return VFS_ERR_INVALID_PATH;
    }

    file->position = (uint32_t)next;
    if (position_out != 0) {
        *position_out = file->position;
    }
    return VFS_OK;
}

int vfs_tell(int fd, uint32_t* position_out) {
    VFSOpenFile* file = vfs_get_open_file(fd);
    if (file == 0 || file->kind != VFS_HANDLE_FILE || position_out == 0) {
        return VFS_ERR_INVALID_PATH;
    }

    *position_out = file->position;
    return VFS_OK;
}

int vfs_close(int fd) {
    VFSOpenFile* file = vfs_get_open_file(fd);
    if (file == 0) {
        return VFS_ERR_INVALID_PATH;
    }

    if (file->kind == VFS_HANDLE_DIR) {
        vfs_reset_open_file(file);
        return VFS_OK;
    }

    if (file->dirty) {
        int result = vfs_write_file(file->path, file->buffer, file->size);
        if (result != VFS_OK) {
            return result;
        }
    }

    vfs_reset_open_file(file);
    return VFS_OK;
}

int vfs_opendir_for_owner(const char* path, uint32_t owner_pid) {
    if (path == 0 || path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }

    const char* relative_path = 0;
    const VFSMount* mount = vfs_find_mount(path, &relative_path);
    if (mount == 0 || mount->ops == 0 || mount->ops->read_dir_entry == 0) {
        return VFS_ERR_NOT_READY;
    }

    VFSFileInfo info;
    if (vfs_get_file_info(path, &info) != VFS_OK || info.type != VFS_NODE_DIR) {
        return VFS_ERR_INVALID_PATH;
    }
    int fd = vfs_alloc_open_slot();
    if (fd < 0) {
        return VFS_ERR_NO_SLOT;
    }

    VFSOpenFile* file = &g_vfs_open_files[fd];
    file->active = 1;
    file->dirty = 0;
    file->kind = VFS_HANDLE_DIR;
    file->mode = 0;
    file->owner_pid = owner_pid;
    file->position = 0;
    file->size = 0;
    file->capacity = 0;
    file->dir_cursor = 0;
    file->buffer = 0;
    vfs_copy_string(file->path, sizeof(file->path), path);
    return fd;
}

int vfs_opendir(const char* path) {
    return vfs_opendir_for_owner(path, 0);
}

int vfs_readdir(int fd, VFSDirEntry* entry) {
    VFSOpenFile* file = vfs_get_open_file(fd);
    if (file == 0 || file->kind != VFS_HANDLE_DIR || entry == 0) {
        return VFS_ERR_INVALID_PATH;
    }

    const char* relative_path = 0;
    const VFSMount* mount = vfs_find_mount(file->path, &relative_path);
    if (mount == 0 || mount->ops == 0 || mount->ops->read_dir_entry == 0) {
        return VFS_ERR_UNSUPPORTED;
    }

    int result = mount->ops->read_dir_entry(mount->backend_ctx,
                                            relative_path != 0 ? relative_path : "",
                                            file->dir_cursor,
                                            entry);
    if (result > 0) {
        file->dir_cursor++;
    }
    return result;
}

int vfs_closedir(int fd) {
    return vfs_close(fd);
}

uint32_t vfs_close_all_for_owner(uint32_t owner_pid) {
    uint32_t count = 0;

    for (int fd = 0; fd < VFS_MAX_OPEN_FILES; fd++) {
        VFSOpenFile* file = &g_vfs_open_files[fd];
        if (!file->active || file->owner_pid != owner_pid) {
            continue;
        }

        vfs_close(fd);
        count++;
    }

    return count;
}

uint32_t vfs_count_open_for_owner(uint32_t owner_pid) {
    uint32_t count = 0;

    for (int fd = 0; fd < VFS_MAX_OPEN_FILES; fd++) {
        if (g_vfs_open_files[fd].active && g_vfs_open_files[fd].owner_pid == owner_pid) {
            count++;
        }
    }

    return count;
}
