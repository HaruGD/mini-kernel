static void fat12_to_name83(const char* path, char out[11]) {
    for (int i = 0; i < 11; i++) {
        out[i] = ' ';
    }

    if (path == 0) {
        return;
    }

    int source = 0;
    while (path[source] == '/') {
        source++;
    }
    int target = 0;
    while (path[source] != '\0' && path[source] != '.' && target < 8) {
        out[target++] = vfs_to_upper_ascii(path[source++]);
    }

    if (path[source] == '.') {
        source++;
        target = 8;
        while (path[source] != '\0' && target < 11) {
            out[target++] = vfs_to_upper_ascii(path[source++]);
        }
    }
}

static int fat12_copy_entry_name(const DirEntry* entry, char* out, uint32_t capacity) {
    if (entry == 0 || out == 0 || capacity < 2) {
        return 0;
    }

    uint32_t n = 0;
    for (uint32_t j = 0; j < 8 && entry->name[j] != ' '; j++) {
        if (n + 1 >= capacity) {
            return 0;
        }
        out[n++] = (char)entry->name[j];
    }

    int has_ext = 0;
    for (uint32_t j = 8; j < 11; j++) {
        if (entry->name[j] != ' ') {
            has_ext = 1;
            break;
        }
    }

    if (has_ext) {
        if (n + 1 >= capacity) {
            return 0;
        }
        out[n++] = '.';
        for (uint32_t j = 8; j < 11 && entry->name[j] != ' '; j++) {
            if (n + 1 >= capacity) {
                return 0;
            }
            out[n++] = (char)entry->name[j];
        }
    }

    out[n] = '\0';
    return 1;
}

static int fat12_lookup_entry(FAT12Driver* fat12, const char* relative_path, DirEntry* entry) {
    if (fat12 == 0) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }

    char name83[11];
    fat12_to_name83(relative_path, name83);
    if (!fat12->find_file(name83, entry)) {
        return VFS_ERR_NOT_FOUND;
    }
    return VFS_OK;
}

static int fat12_backend_list_files(void* backend_ctx, const char* relative_path) {
    FAT12Driver* fat12 = (FAT12Driver*)backend_ctx;
    if (fat12 == 0) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path != 0 && relative_path[0] != '\0') {
        return VFS_ERR_UNSUPPORTED;
    }
    fat12->list_files();
    return VFS_OK;
}

static int fat12_backend_read_dir_entry(void* backend_ctx, const char* relative_path, uint32_t cursor, VFSDirEntry* entry) {
    FAT12Driver* fat12 = (FAT12Driver*)backend_ctx;
    if (fat12 == 0 || entry == 0) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path != 0 && relative_path[0] != '\0') {
        return VFS_ERR_UNSUPPORTED;
    }

    DirEntry dir_entry;
    if (!fat12->read_root_entry(cursor, &dir_entry)) {
        return 0;
    }

    entry->type = (dir_entry.attributes & 0x10) ? VFS_NODE_DIR : VFS_NODE_FILE;
    entry->size = dir_entry.file_size;
    if (!fat12_copy_entry_name(&dir_entry, entry->name, sizeof(entry->name))) {
        return VFS_ERR_IO;
    }
    return 1;
}

static int fat12_backend_get_file_info(void* backend_ctx, const char* relative_path, VFSFileInfo* info) {
    DirEntry entry;
    int result = fat12_lookup_entry((FAT12Driver*)backend_ctx, relative_path, &entry);
    if (result != VFS_OK) {
        return result;
    }

    if (info != 0) {
        info->type = VFS_NODE_FILE;
        info->size = entry.file_size;
    }
    return VFS_OK;
}

static int fat12_backend_read_file(void* backend_ctx, const char* relative_path, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read_out) {
    FAT12Driver* fat12 = (FAT12Driver*)backend_ctx;
    DirEntry entry;
    int result = fat12_lookup_entry(fat12, relative_path, &entry);
    if (result != VFS_OK) {
        return result;
    }

    uint32_t required_size = entry.file_size > 0 ? entry.file_size : 1;
    if (buffer == 0 || buffer_size < required_size) {
        return VFS_ERR_BUFFER_TOO_SMALL;
    }

    if (fat12->read_file(&entry, buffer) < 0) {
        return VFS_ERR_IO;
    }

    if (bytes_read_out != 0) {
        *bytes_read_out = entry.file_size;
    }
    return VFS_OK;
}

static int fat12_backend_write_file(void* backend_ctx, const char* relative_path, const uint8_t* buffer, uint32_t size) {
    FAT12Driver* fat12 = (FAT12Driver*)backend_ctx;
    if (fat12 == 0) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    if (size > 0 && buffer == 0) {
        return VFS_ERR_IO;
    }

    char name83[11];
    uint8_t dummy = 0;
    fat12_to_name83(relative_path, name83);
    const uint8_t* write_buffer = size == 0 ? &dummy : buffer;
    if (!fat12->write_file(name83, (uint8_t*)write_buffer, size)) {
        return VFS_ERR_IO;
    }
    return VFS_OK;
}

static int fat12_backend_touch_file(void* backend_ctx, const char* relative_path) {
    return fat12_backend_write_file(backend_ctx, relative_path, 0, 0);
}

static int fat12_backend_delete_file(void* backend_ctx, const char* relative_path) {
    FAT12Driver* fat12 = (FAT12Driver*)backend_ctx;
    if (fat12 == 0) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }

    char name83[11];
    fat12_to_name83(relative_path, name83);
    if (!fat12->delete_file(name83)) {
        return VFS_ERR_NOT_FOUND;
    }
    return VFS_OK;
}

static const VFSBackendOps g_fat12_backend_ops = {
    fat12_backend_list_files,
    fat12_backend_read_dir_entry,
    fat12_backend_get_file_info,
    fat12_backend_read_file,
    fat12_backend_write_file,
    fat12_backend_touch_file,
    fat12_backend_delete_file,
    0,
    0,
    0,
};
