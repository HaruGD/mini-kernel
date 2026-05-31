#include "fs/vfs.h"

#include "heap.h"
#include "drivers/terminal.h"
#include "fs/fat12.h"

extern Terminal terminal;

#define VFS_MAX_MOUNTS 4
#define VFS_MAX_OPEN_FILES 8
#define MEMFS_MAX_FILES 4
#define MEMFS_MAX_NAME 16
#define MEMFS_MAX_FILE_SIZE 128

struct VFSMount {
    uint8_t active;
    char mount_path[16];
    char fs_name[16];
    uint32_t backend_kind;
    const VFSBackendOps* ops;
    void* backend_ctx;
};

static VFSMount g_vfs_mounts[VFS_MAX_MOUNTS];

struct VFSOpenFile {
    uint8_t active;
    uint8_t dirty;
    uint16_t mode;
    uint32_t position;
    uint32_t size;
    uint32_t capacity;
    char path[32];
    uint8_t* buffer;
};

static VFSOpenFile g_vfs_open_files[VFS_MAX_OPEN_FILES];

struct MemFSFile {
    uint8_t active;
    char name[MEMFS_MAX_NAME];
    uint16_t size;
    uint8_t data[MEMFS_MAX_FILE_SIZE];
};

static MemFSFile g_memfs_files[MEMFS_MAX_FILES];

static uint32_t vfs_strlen_local(const char* text) {
    uint32_t len = 0;
    while (text[len] != '\0') {
        len++;
    }
    return len;
}

static void vfs_copy_string(char* dest, uint32_t capacity, const char* src) {
    uint32_t i = 0;
    if (capacity == 0) {
        return;
    }

    while (src != 0 && src[i] != '\0' && i + 1 < capacity) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static int vfs_str_eq(const char* a, const char* b) {
    uint32_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static void vfs_mem_copy(uint8_t* dest, const uint8_t* src, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        dest[i] = src[i];
    }
}

static char vfs_to_upper_ascii(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - ('a' - 'A'));
    }
    return ch;
}

static void fat12_to_name83(const char* path, char out[11]) {
    for (int i = 0; i < 11; i++) {
        out[i] = ' ';
    }

    if (path == 0) {
        return;
    }

    int source = 0;
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

static int fat12_backend_list_files(void* backend_ctx, const char*) {
    FAT12Driver* fat12 = (FAT12Driver*)backend_ctx;
    if (fat12 == 0) {
        return VFS_ERR_NOT_READY;
    }
    fat12->list_files();
    return VFS_OK;
}

static int fat12_backend_get_file_info(void* backend_ctx, const char* relative_path, VFSFileInfo* info) {
    DirEntry entry;
    int result = fat12_lookup_entry((FAT12Driver*)backend_ctx, relative_path, &entry);
    if (result != VFS_OK) {
        return result;
    }

    if (info != 0) {
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
    fat12_backend_get_file_info,
    fat12_backend_read_file,
    fat12_backend_write_file,
    fat12_backend_touch_file,
    fat12_backend_delete_file,
};

static MemFSFile* memfs_find_file(const char* name) {
    for (uint32_t i = 0; i < MEMFS_MAX_FILES; i++) {
        if (g_memfs_files[i].active && vfs_str_eq(g_memfs_files[i].name, name)) {
            return &g_memfs_files[i];
        }
    }
    return 0;
}

static MemFSFile* memfs_alloc_file(const char* name) {
    for (uint32_t i = 0; i < MEMFS_MAX_FILES; i++) {
        if (!g_memfs_files[i].active) {
            g_memfs_files[i].active = 1;
            vfs_copy_string(g_memfs_files[i].name, sizeof(g_memfs_files[i].name), name);
            g_memfs_files[i].size = 0;
            return &g_memfs_files[i];
        }
    }
    return 0;
}

static int memfs_backend_list_files(void*, const char* relative_path) {
    if (relative_path != 0 && relative_path[0] != '\0') {
        return VFS_ERR_INVALID_PATH;
    }

    for (uint32_t i = 0; i < MEMFS_MAX_FILES; i++) {
        if (!g_memfs_files[i].active) {
            continue;
        }
        terminal.print("\n");
        terminal.print(g_memfs_files[i].name);
    }
    return VFS_OK;
}

static int memfs_backend_get_file_info(void*, const char* relative_path, VFSFileInfo* info) {
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }

    MemFSFile* file = memfs_find_file(relative_path);
    if (file == 0) {
        return VFS_ERR_NOT_FOUND;
    }

    if (info != 0) {
        info->size = file->size;
    }
    return VFS_OK;
}

static int memfs_backend_read_file(void*, const char* relative_path, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read_out) {
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }

    MemFSFile* file = memfs_find_file(relative_path);
    if (file == 0) {
        return VFS_ERR_NOT_FOUND;
    }
    if (buffer == 0 || buffer_size < (file->size > 0 ? file->size : 1)) {
        return VFS_ERR_BUFFER_TOO_SMALL;
    }

    for (uint32_t i = 0; i < file->size; i++) {
        buffer[i] = file->data[i];
    }
    if (bytes_read_out != 0) {
        *bytes_read_out = file->size;
    }
    return VFS_OK;
}

static int memfs_backend_write_file(void*, const char* relative_path, const uint8_t* buffer, uint32_t size) {
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    if (vfs_strlen_local(relative_path) >= MEMFS_MAX_NAME || size > MEMFS_MAX_FILE_SIZE) {
        return VFS_ERR_IO;
    }
    if (size > 0 && buffer == 0) {
        return VFS_ERR_IO;
    }

    MemFSFile* file = memfs_find_file(relative_path);
    if (file == 0) {
        file = memfs_alloc_file(relative_path);
        if (file == 0) {
            return VFS_ERR_IO;
        }
    }

    for (uint32_t i = 0; i < size; i++) {
        file->data[i] = buffer[i];
    }
    file->size = (uint16_t)size;
    return VFS_OK;
}

static int memfs_backend_touch_file(void* backend_ctx, const char* relative_path) {
    return memfs_backend_write_file(backend_ctx, relative_path, 0, 0);
}

static int memfs_backend_delete_file(void*, const char* relative_path) {
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }

    MemFSFile* file = memfs_find_file(relative_path);
    if (file == 0) {
        return VFS_ERR_NOT_FOUND;
    }

    file->active = 0;
    file->name[0] = '\0';
    file->size = 0;
    return VFS_OK;
}

static const VFSBackendOps g_memfs_backend_ops = {
    memfs_backend_list_files,
    memfs_backend_get_file_info,
    memfs_backend_read_file,
    memfs_backend_write_file,
    memfs_backend_touch_file,
    memfs_backend_delete_file,
};

static void vfs_reset_open_file(VFSOpenFile* file) {
    if (file == 0) {
        return;
    }

    file->active = 0;
    file->dirty = 0;
    file->mode = 0;
    file->position = 0;
    file->size = 0;
    file->capacity = 0;
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

void vfs_init() {
    for (uint32_t i = 0; i < VFS_MAX_MOUNTS; i++) {
        vfs_reset_mount(&g_vfs_mounts[i]);
    }
    for (uint32_t i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        g_vfs_open_files[i].buffer = 0;
        vfs_reset_open_file(&g_vfs_open_files[i]);
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

int vfs_mount_fat12_root(FAT12Driver* fat12) {
    if (fat12 == 0) {
        return VFS_ERR_NOT_READY;
    }
    return vfs_mount_root("fat12", VFS_BACKEND_FAT12, &g_fat12_backend_ops, fat12);
}

int vfs_mount_memfs(const char* mount_path) {
    for (uint32_t i = 0; i < MEMFS_MAX_FILES; i++) {
        g_memfs_files[i].active = 0;
        g_memfs_files[i].name[0] = '\0';
        g_memfs_files[i].size = 0;
    }
    return vfs_mount(mount_path, "memfs", VFS_BACKEND_MEMFS, &g_memfs_backend_ops, g_memfs_files);
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
    if (mount == 0 || mount->ops == 0 || mount->ops->list_files == 0) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0) {
        relative_path = "";
    }
    return mount->ops->list_files(mount->backend_ctx, relative_path);
}

int vfs_get_file_info(const char* path, VFSFileInfo* info) {
    const char* relative_path = 0;
    const VFSMount* mount = vfs_find_mount(path, &relative_path);
    if (mount == 0 || mount->ops == 0 || mount->ops->get_file_info == 0) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
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

int vfs_open(const char* path, uint32_t mode) {
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

    for (int fd = 0; fd < VFS_MAX_OPEN_FILES; fd++) {
        VFSOpenFile* file = &g_vfs_open_files[fd];
        if (file->active) {
            continue;
        }

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
        file->mode = (uint16_t)mode;
        file->position = 0;
        file->size = initial_size;
        file->capacity = initial_capacity;
        vfs_copy_string(file->path, sizeof(file->path), path);

        if (initial_size > 0) {
            uint32_t bytes_read = 0;
            if (vfs_read_file(path, file->buffer, file->capacity, &bytes_read) != VFS_OK) {
                vfs_reset_open_file(file);
                return VFS_ERR_IO;
            }
            file->size = bytes_read;
        }

        return fd;
    }

    return VFS_ERR_NO_SLOT;
}

int vfs_read(int fd, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read_out) {
    VFSOpenFile* file = vfs_get_open_file(fd);
    if (file == 0 || buffer == 0) {
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
    if (file == 0 || (size > 0 && buffer == 0)) {
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

int vfs_close(int fd) {
    VFSOpenFile* file = vfs_get_open_file(fd);
    if (file == 0) {
        return VFS_ERR_INVALID_PATH;
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
