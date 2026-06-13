static void vfs_reset_open_file(VFSOpenFile* file) {
    if (file == 0) {
        return;
    }

    file->active = 0;
    file->dirty = 0;
    file->kind = VFS_HANDLE_NONE;
    file->mode = 0;
    file->owner_pid = 0;
    file->position = 0;
    file->size = 0;
    file->capacity = 0;
    file->dir_cursor = 0;
    file->path[0] = '\0';
    if (file->buffer != 0) {
        kfree(file->buffer);
        file->buffer = 0;
    }
}

static VFSOpenFile* vfs_get_open_file(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES) {
        return 0;
    }
    if (!g_vfs_open_files[fd].active) {
        return 0;
    }
    return &g_vfs_open_files[fd];
}

static int vfs_alloc_open_slot() {
    for (int fd = 0; fd < VFS_MAX_OPEN_FILES; fd++) {
        if (!g_vfs_open_files[fd].active) {
            return fd;
        }
    }
    return VFS_ERR_NO_SLOT;
}

static uint32_t vfs_growth_capacity(uint32_t required) {
    uint32_t capacity = 64;
    while (capacity < required) {
        capacity *= 2;
    }
    return capacity;
}

static int vfs_ensure_open_file_capacity(VFSOpenFile* file, uint32_t required) {
    if (file == 0) {
        return VFS_ERR_IO;
    }
    if (required <= file->capacity) {
        return VFS_OK;
    }

    uint32_t new_capacity = vfs_growth_capacity(required);
    uint8_t* new_buffer = (uint8_t*)kmalloc(new_capacity);
    if (new_buffer == 0) {
        return VFS_ERR_IO;
    }

    if (file->buffer != 0 && file->size > 0) {
        vfs_mem_copy(new_buffer, file->buffer, file->size);
        kfree(file->buffer);
    }

    file->buffer = new_buffer;
    file->capacity = new_capacity;
    return VFS_OK;
}

static void vfs_reset_mount(VFSMount* mount) {
    mount->active = 0;
    mount->mount_path[0] = '\0';
    mount->fs_name[0] = '\0';
    mount->backend_kind = VFS_BACKEND_NONE;
    mount->ops = 0;
    mount->backend_ctx = 0;
}

static const VFSMount* vfs_find_mount(const char* path, const char** relative_path_out) {
    if (path == 0 || path[0] == '\0') {
        return 0;
    }

    const VFSMount* best_mount = 0;
    uint32_t best_length = 0;
    const char* best_relative = 0;

    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
        const VFSMount* mount = &g_vfs_mounts[i];
        if (!mount->active) {
            continue;
        }

        uint32_t mount_len = vfs_strlen_local(mount->mount_path);
        const char* relative = 0;

        if (vfs_str_eq(mount->mount_path, "/")) {
            relative = path[0] == '/' ? path + 1 : path;
        } else {
            uint32_t j = 0;
            while (j < mount_len && mount->mount_path[j] == path[j]) {
                j++;
            }
            if (j != mount_len) {
                continue;
            }
            if (path[j] == '/') {
                relative = path + j + 1;
            } else if (path[j] == '\0') {
                relative = path + j;
            } else {
                continue;
            }
        }

        if (mount_len >= best_length) {
            best_mount = mount;
            best_length = mount_len;
            best_relative = relative;
        }
    }

    if (best_mount != 0 && relative_path_out != 0) {
        *relative_path_out = best_relative;
    }
    return best_mount;
}

static int vfs_path_is_descendant_or_same(const char* path, const char* prefix) {
    uint32_t i = 0;
    if (path == 0 || prefix == 0) {
        return 0;
    }
    while (prefix[i] != '\0' && path[i] != '\0' && prefix[i] == path[i]) {
        i++;
    }
    if (prefix[i] != '\0') {
        return 0;
    }
    if (path[i] == '\0') {
        return 1;
    }
    if (prefix[i - (i > 0 ? 1u : 0u)] == '/') {
        return 1;
    }
    return path[i] == '/';
}

static int vfs_has_open_descendant_path(const char* path) {
    for (uint32_t i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        VFSOpenFile* file = &g_vfs_open_files[i];
        if (!file->active) {
            continue;
        }
        if (vfs_path_is_descendant_or_same(file->path, path)) {
            return 1;
        }
    }
    return 0;
}

static int vfs_list_directory_generic(const VFSMount* mount, const char* relative_path) {
    if (mount == 0 || mount->ops == 0 || mount->ops->read_dir_entry == 0) {
        return VFS_ERR_UNSUPPORTED;
    }

    for (uint32_t index = 0;; index++) {
        VFSDirEntry entry;
        int result = mount->ops->read_dir_entry(mount->backend_ctx,
                                                relative_path != 0 ? relative_path : "",
                                                index,
                                                &entry);
        if (result < 0) {
            return result;
        }
        if (result == 0) {
            return VFS_OK;
        }
        vfs_print_dir_entry(&entry);
    }
}

void vfs_init() {
    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
        vfs_reset_mount(&g_vfs_mounts[i]);
    }
    for (uint32_t i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        g_vfs_open_files[i].buffer = 0;
        vfs_reset_open_file(&g_vfs_open_files[i]);
    }
    for (uint32_t i = 0; i < MEMFS_MAX_NODES; i++) {
        g_memfs_nodes[i].active = 0;
        g_memfs_nodes[i].type = VFS_NODE_NONE;
        g_memfs_nodes[i].parent = -1;
        g_memfs_nodes[i].first_child = -1;
        g_memfs_nodes[i].next_sibling = -1;
        g_memfs_nodes[i].name[0] = '\0';
        g_memfs_nodes[i].size = 0;
    }
}

int vfs_mount(const char* mount_path, const char* fs_name, uint32_t backend_kind, const VFSBackendOps* ops, void* backend_ctx) {
    if (ops == 0 || backend_ctx == 0) {
        return VFS_ERR_INVALID_PATH;
    }
    if (mount_path == 0 || mount_path[0] != '/') {
        return VFS_ERR_INVALID_PATH;
    }

    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_vfs_mounts[i].active && vfs_str_eq(g_vfs_mounts[i].mount_path, mount_path)) {
            return VFS_ERR_ALREADY_MOUNTED;
        }
    }

    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
        VFSMount* mount = &g_vfs_mounts[i];
        if (mount->active) {
            continue;
        }

        mount->active = 1;
        vfs_copy_string(mount->mount_path, sizeof(mount->mount_path), mount_path);
        vfs_copy_string(mount->fs_name, sizeof(mount->fs_name), fs_name != 0 ? fs_name : "unknown");
        mount->backend_kind = backend_kind;
        mount->ops = ops;
        mount->backend_ctx = backend_ctx;
        return VFS_OK;
    }

    return VFS_ERR_NO_SLOT;
}

int vfs_mount_root(const char* fs_name, uint32_t backend_kind, const VFSBackendOps* ops, void* backend_ctx) {
    return vfs_mount("/", fs_name, backend_kind, ops, backend_ctx);
}

int vfs_mount_memfs(const char* mount_path) {
    for (uint32_t i = 0; i < MEMFS_MAX_NODES; i++) {
        g_memfs_nodes[i].active = 0;
        g_memfs_nodes[i].type = VFS_NODE_NONE;
        g_memfs_nodes[i].parent = -1;
        g_memfs_nodes[i].first_child = -1;
        g_memfs_nodes[i].next_sibling = -1;
        g_memfs_nodes[i].name[0] = '\0';
        g_memfs_nodes[i].size = 0;
    }
    g_memfs_nodes[0].active = 1;
    g_memfs_nodes[0].type = VFS_NODE_DIR;
    g_memfs_nodes[0].parent = -1;
    g_memfs_nodes[0].first_child = -1;
    g_memfs_nodes[0].next_sibling = -1;
    g_memfs_nodes[0].name[0] = '\0';
    g_memfs_nodes[0].size = 0;
    return vfs_mount(mount_path, "memfs", VFS_BACKEND_MEMFS, &g_memfs_backend_ops, g_memfs_nodes);
}

uint32_t vfs_mount_count() {
    uint32_t count = 0;
    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_vfs_mounts[i].active) {
            count++;
        }
    }
    return count;
}

int vfs_get_mount_info(uint32_t index, VFSMountInfo* info) {
    uint32_t current = 0;
    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
        const VFSMount* mount = &g_vfs_mounts[i];
        if (!mount->active) {
            continue;
        }
        if (current == index) {
            if (info != 0) {
                vfs_copy_string(info->mount_path, sizeof(info->mount_path), mount->mount_path);
                vfs_copy_string(info->fs_name, sizeof(info->fs_name), mount->fs_name);
                info->backend_kind = mount->backend_kind;
            }
            return VFS_OK;
        }
        current++;
    }
    return VFS_ERR_NOT_FOUND;
}

int vfs_list_files() {
    return vfs_list_files_at("/");
}

int vfs_list_files_at(const char* path) {
    const char* relative_path = 0;
    const VFSMount* mount = vfs_find_mount(path != 0 ? path : "/", &relative_path);
    if (mount == 0 || mount->ops == 0) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0) {
        relative_path = "";
    }

    if (mount->ops->read_dir_entry != 0) {
        VFSFileInfo info;
        if (vfs_get_file_info(path != 0 ? path : "/", &info) == VFS_OK && info.type == VFS_NODE_DIR) {
            return vfs_list_directory_generic(mount, relative_path);
        }
    }

    if (mount->ops->list_files == 0) {
        return VFS_ERR_UNSUPPORTED;
    }
    return mount->ops->list_files(mount->backend_ctx, relative_path);
}

int vfs_get_file_info(const char* path, VFSFileInfo* info) {
    const char* relative_path = 0;
    const VFSMount* mount = vfs_find_mount(path, &relative_path);
    if (mount == 0 || mount->ops == 0 || mount->ops->get_file_info == 0) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0) {
        relative_path = "";
    }
    if (relative_path[0] == '\0') {
        if (info != 0) {
            info->type = VFS_NODE_DIR;
            info->size = 0;
        }
        return VFS_OK;
    }
    return mount->ops->get_file_info(mount->backend_ctx, relative_path, info);
}

int vfs_read_file(const char* path, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read_out) {
    const char* relative_path = 0;
    const VFSMount* mount = vfs_find_mount(path, &relative_path);
    if (mount == 0 || mount->ops == 0 || mount->ops->read_file == 0) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    return mount->ops->read_file(mount->backend_ctx, relative_path, buffer, buffer_size, bytes_read_out);
}

int vfs_write_file(const char* path, const uint8_t* buffer, uint32_t size) {
    const char* relative_path = 0;
    const VFSMount* mount = vfs_find_mount(path, &relative_path);
    if (mount == 0 || mount->ops == 0 || mount->ops->write_file == 0) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    return mount->ops->write_file(mount->backend_ctx, relative_path, buffer, size);
}

int vfs_touch_file(const char* path) {
    const char* relative_path = 0;
    const VFSMount* mount = vfs_find_mount(path, &relative_path);
    if (mount == 0 || mount->ops == 0 || mount->ops->touch_file == 0) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    return mount->ops->touch_file(mount->backend_ctx, relative_path);
}

int vfs_delete_file(const char* path) {
    const char* relative_path = 0;
    const VFSMount* mount = vfs_find_mount(path, &relative_path);
    if (mount == 0 || mount->ops == 0 || mount->ops->delete_file == 0) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    return mount->ops->delete_file(mount->backend_ctx, relative_path);
}

int vfs_mkdir(const char* path) {
    const char* relative_path = 0;
    const VFSMount* mount = vfs_find_mount(path, &relative_path);
    if (mount == 0 || mount->ops == 0 || mount->ops->mkdir == 0) {
        return VFS_ERR_UNSUPPORTED;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    return mount->ops->mkdir(mount->backend_ctx, relative_path);
}

int vfs_rmdir(const char* path) {
    const char* relative_path = 0;
    const VFSMount* mount = vfs_find_mount(path, &relative_path);
    if (mount == 0 || mount->ops == 0 || mount->ops->rmdir == 0) {
        return VFS_ERR_UNSUPPORTED;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    return mount->ops->rmdir(mount->backend_ctx, relative_path);
}

int vfs_rename(const char* old_path, const char* new_path) {
    const char* old_relative_path = 0;
    const char* new_relative_path = 0;
    const VFSMount* old_mount = vfs_find_mount(old_path, &old_relative_path);
    const VFSMount* new_mount = vfs_find_mount(new_path, &new_relative_path);
    if (old_mount == 0 || new_mount == 0 || old_mount != new_mount) {
        return VFS_ERR_UNSUPPORTED;
    }
    if (old_mount->ops == 0 || old_mount->ops->rename_path == 0) {
        return VFS_ERR_UNSUPPORTED;
    }
    if (old_relative_path == 0 || old_relative_path[0] == '\0' ||
        new_relative_path == 0 || new_relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    if (vfs_has_open_descendant_path(old_path)) {
        return VFS_ERR_UNSUPPORTED;
    }
    return old_mount->ops->rename_path(old_mount->backend_ctx, old_relative_path, new_relative_path);
}

#include "fs/vfs/vfs_open.cpp"
