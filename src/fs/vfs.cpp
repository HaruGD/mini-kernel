#include "fs/vfs.h"

#include "heap.h"
#include "arch/x86/io.h"
#include "drivers/terminal.h"
#include "fs/fat12.h"

extern Terminal terminal;

#define VFS_MAX_MOUNTS 4
#define VFS_MAX_OPEN_FILES 8
#define MEMFS_MAX_NODES 16
#define MEMFS_MAX_NAME 16
#define MEMFS_MAX_FILE_SIZE 256

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
    uint32_t owner_pid;
    uint32_t position;
    uint32_t size;
    uint32_t capacity;
    char path[32];
    uint8_t* buffer;
};

static VFSOpenFile g_vfs_open_files[VFS_MAX_OPEN_FILES];

struct MemFSNode {
    uint8_t active;
    uint8_t type;
    int16_t parent;
    int16_t first_child;
    int16_t next_sibling;
    char name[MEMFS_MAX_NAME];
    uint16_t size;
    uint8_t data[MEMFS_MAX_FILE_SIZE];
};

static MemFSNode g_memfs_nodes[MEMFS_MAX_NODES];

static int vfs_serial_ready() {
    return inb(0x3FD) & 0x20;
}

static void vfs_serial_putchar(char c) {
    while (!vfs_serial_ready()) {
    }
    outb(0x3F8, (unsigned char)c);
}

static void vfs_putchar_both(char c) {
    terminal.putchar(c);
    if (c == '\n') {
        vfs_serial_putchar('\r');
    }
    vfs_serial_putchar(c);
}

static void vfs_print(const char* text) {
    if (text == 0) {
        return;
    }

    for (uint32_t i = 0; text[i] != '\0'; i++) {
        vfs_putchar_both(text[i]);
    }
}

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
    fat12_backend_get_file_info,
    fat12_backend_read_file,
    fat12_backend_write_file,
    fat12_backend_touch_file,
    fat12_backend_delete_file,
    0,
    0,
};

static MemFSNode* memfs_get_node(int16_t index) {
    if (index < 0 || index >= MEMFS_MAX_NODES) {
        return 0;
    }
    if (!g_memfs_nodes[index].active) {
        return 0;
    }
    return &g_memfs_nodes[index];
}

static int memfs_is_reserved_segment(const char* name) {
    return vfs_str_eq(name, ".") || vfs_str_eq(name, "..");
}

static int memfs_find_child(int16_t dir_index, const char* name) {
    MemFSNode* dir = memfs_get_node(dir_index);
    if (dir == 0 || dir->type != VFS_NODE_DIR) {
        return -1;
    }

    int16_t child_index = dir->first_child;
    while (child_index >= 0) {
        MemFSNode* child = memfs_get_node(child_index);
        if (child != 0 && vfs_str_eq(child->name, name)) {
            return child_index;
        }
        child_index = child != 0 ? child->next_sibling : -1;
    }
    return -1;
}

static int memfs_alloc_node(uint8_t type, int16_t parent, const char* name) {
    if (name == 0 || name[0] == '\0' || vfs_strlen_local(name) >= MEMFS_MAX_NAME || memfs_is_reserved_segment(name)) {
        return -1;
    }

    for (int16_t i = 1; i < MEMFS_MAX_NODES; i++) {
        if (g_memfs_nodes[i].active) {
            continue;
        }

        g_memfs_nodes[i].active = 1;
        g_memfs_nodes[i].type = type;
        g_memfs_nodes[i].parent = parent;
        g_memfs_nodes[i].first_child = -1;
        g_memfs_nodes[i].next_sibling = -1;
        g_memfs_nodes[i].size = 0;
        vfs_copy_string(g_memfs_nodes[i].name, sizeof(g_memfs_nodes[i].name), name);

        MemFSNode* parent_node = memfs_get_node(parent);
        if (parent_node != 0) {
            g_memfs_nodes[i].next_sibling = parent_node->first_child;
            parent_node->first_child = i;
        }

        return i;
    }

    return -1;
}

static void memfs_unlink_child(int16_t parent_index, int16_t child_index) {
    MemFSNode* parent = memfs_get_node(parent_index);
    if (parent == 0) {
        return;
    }

    int16_t current = parent->first_child;
    int16_t previous = -1;
    while (current >= 0) {
        MemFSNode* node = memfs_get_node(current);
        int16_t next = node != 0 ? node->next_sibling : -1;
        if (current == child_index) {
            if (previous < 0) {
                parent->first_child = next;
            } else {
                MemFSNode* previous_node = memfs_get_node(previous);
                if (previous_node != 0) {
                    previous_node->next_sibling = next;
                }
            }
            return;
        }
        previous = current;
        current = next;
    }
}

static void memfs_reset_node(int16_t index) {
    MemFSNode* node = memfs_get_node(index);
    if (node == 0) {
        return;
    }

    if (node->parent >= 0) {
        memfs_unlink_child(node->parent, index);
    }

    node->active = 0;
    node->type = VFS_NODE_NONE;
    node->parent = -1;
    node->first_child = -1;
    node->next_sibling = -1;
    node->name[0] = '\0';
    node->size = 0;
}

static int memfs_next_segment(const char** cursor_inout, char* segment_out) {
    const char* cursor = *cursor_inout;
    uint32_t length = 0;

    if (cursor == 0) {
        return 0;
    }

    while (*cursor == '/') {
        return -1;
    }
    if (*cursor == '\0') {
        return 0;
    }

    while (cursor[length] != '\0' && cursor[length] != '/') {
        if (length + 1 >= MEMFS_MAX_NAME) {
            return -1;
        }
        segment_out[length] = cursor[length];
        length++;
    }
    segment_out[length] = '\0';
    if (length == 0 || memfs_is_reserved_segment(segment_out)) {
        return -1;
    }

    cursor += length;
    if (*cursor == '/') {
        cursor++;
        if (*cursor == '\0') {
            return -1;
        }
    }
    *cursor_inout = cursor;
    return 1;
}

static int memfs_lookup_path(const char* relative_path, int16_t* out_node_index) {
    const char* cursor = relative_path;
    int16_t current = 0;

    if (out_node_index == 0) {
        return VFS_ERR_IO;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        *out_node_index = 0;
        return VFS_OK;
    }

    while (1) {
        char segment[MEMFS_MAX_NAME];
        int step = memfs_next_segment(&cursor, segment);
        if (step < 0) {
            return VFS_ERR_INVALID_PATH;
        }
        if (step == 0) {
            *out_node_index = current;
            return VFS_OK;
        }

        int child = memfs_find_child(current, segment);
        if (child < 0) {
            return VFS_ERR_NOT_FOUND;
        }
        current = (int16_t)child;
    }
}

static int memfs_lookup_parent(const char* relative_path, int16_t* out_parent_index, char* out_leaf_name) {
    const char* cursor = relative_path;
    int16_t current = 0;
    char segment[MEMFS_MAX_NAME];

    if (relative_path == 0 || relative_path[0] == '\0' || out_parent_index == 0 || out_leaf_name == 0) {
        return VFS_ERR_INVALID_PATH;
    }

    while (1) {
        const char* next_cursor = cursor;
        int step = memfs_next_segment(&next_cursor, segment);
        if (step <= 0) {
            return VFS_ERR_INVALID_PATH;
        }

        if (*next_cursor == '\0') {
            *out_parent_index = current;
            vfs_copy_string(out_leaf_name, MEMFS_MAX_NAME, segment);
            return VFS_OK;
        }

        int child = memfs_find_child(current, segment);
        if (child < 0) {
            return VFS_ERR_NOT_FOUND;
        }

        MemFSNode* child_node = memfs_get_node((int16_t)child);
        if (child_node == 0 || child_node->type != VFS_NODE_DIR) {
            return VFS_ERR_NOT_FOUND;
        }

        current = (int16_t)child;
        cursor = next_cursor;
    }
}

static int memfs_backend_list_files(void*, const char* relative_path) {
    int16_t dir_index = 0;
    int result = memfs_lookup_path(relative_path != 0 ? relative_path : "", &dir_index);
    if (result != VFS_OK) {
        return result;
    }

    MemFSNode* dir = memfs_get_node(dir_index);
    if (dir == 0 || dir->type != VFS_NODE_DIR) {
        return VFS_ERR_INVALID_PATH;
    }

    int16_t child_index = dir->first_child;
    while (child_index >= 0) {
        MemFSNode* child = memfs_get_node(child_index);
        if (child != 0) {
            vfs_print("\n");
            vfs_print(child->name);
            if (child->type == VFS_NODE_DIR) {
                vfs_print("/");
            }
        }
        child_index = child != 0 ? child->next_sibling : -1;
    }
    return VFS_OK;
}

static int memfs_backend_get_file_info(void*, const char* relative_path, VFSFileInfo* info) {
    int16_t node_index = 0;
    int result = memfs_lookup_path(relative_path, &node_index);
    if (result != VFS_OK) {
        return result;
    }

    MemFSNode* node = memfs_get_node(node_index);
    if (node == 0) {
        return VFS_ERR_NOT_FOUND;
    }

    if (info != 0) {
        info->type = node->type;
        info->size = node->type == VFS_NODE_FILE ? node->size : 0;
    }
    return VFS_OK;
}

static int memfs_backend_read_file(void*, const char* relative_path, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read_out) {
    int16_t node_index = 0;
    int result = memfs_lookup_path(relative_path, &node_index);
    if (result != VFS_OK) {
        return result;
    }

    MemFSNode* node = memfs_get_node(node_index);
    if (node == 0 || node->type != VFS_NODE_FILE) {
        return VFS_ERR_INVALID_PATH;
    }
    if (buffer == 0 || buffer_size < (node->size > 0 ? node->size : 1)) {
        return VFS_ERR_BUFFER_TOO_SMALL;
    }

    for (uint32_t i = 0; i < node->size; i++) {
        buffer[i] = node->data[i];
    }
    if (bytes_read_out != 0) {
        *bytes_read_out = node->size;
    }
    return VFS_OK;
}

static int memfs_backend_write_file(void*, const char* relative_path, const uint8_t* buffer, uint32_t size) {
    int16_t parent_index = 0;
    char leaf_name[MEMFS_MAX_NAME];
    if (size > MEMFS_MAX_FILE_SIZE || (size > 0 && buffer == 0)) {
        return VFS_ERR_IO;
    }

    int result = memfs_lookup_parent(relative_path, &parent_index, leaf_name);
    if (result != VFS_OK) {
        return result;
    }

    int child = memfs_find_child(parent_index, leaf_name);
    MemFSNode* node = child >= 0 ? memfs_get_node((int16_t)child) : 0;
    if (node == 0) {
        int allocated = memfs_alloc_node(VFS_NODE_FILE, parent_index, leaf_name);
        if (allocated < 0) {
            return VFS_ERR_IO;
        }
        node = memfs_get_node((int16_t)allocated);
    } else if (node->type != VFS_NODE_FILE) {
        return VFS_ERR_INVALID_PATH;
    }

    for (uint32_t i = 0; i < size; i++) {
        node->data[i] = buffer[i];
    }
    node->size = (uint16_t)size;
    return VFS_OK;
}

static int memfs_backend_touch_file(void* backend_ctx, const char* relative_path) {
    return memfs_backend_write_file(backend_ctx, relative_path, 0, 0);
}

static int memfs_backend_delete_file(void*, const char* relative_path) {
    int16_t node_index = 0;
    int result = memfs_lookup_path(relative_path, &node_index);
    if (result != VFS_OK) {
        return result;
    }

    MemFSNode* node = memfs_get_node(node_index);
    if (node == 0 || node->type != VFS_NODE_FILE) {
        return VFS_ERR_INVALID_PATH;
    }

    memfs_reset_node(node_index);
    return VFS_OK;
}

static int memfs_backend_mkdir(void*, const char* relative_path) {
    int16_t parent_index = 0;
    char leaf_name[MEMFS_MAX_NAME];
    int result = memfs_lookup_parent(relative_path, &parent_index, leaf_name);
    if (result != VFS_OK) {
        return result;
    }
    if (memfs_find_child(parent_index, leaf_name) >= 0) {
        return VFS_ERR_ALREADY_MOUNTED;
    }
    return memfs_alloc_node(VFS_NODE_DIR, parent_index, leaf_name) >= 0 ? VFS_OK : VFS_ERR_IO;
}

static int memfs_backend_rmdir(void*, const char* relative_path) {
    int16_t node_index = 0;
    int result = memfs_lookup_path(relative_path, &node_index);
    if (result != VFS_OK) {
        return result;
    }
    if (node_index == 0) {
        return VFS_ERR_UNSUPPORTED;
    }

    MemFSNode* node = memfs_get_node(node_index);
    if (node == 0 || node->type != VFS_NODE_DIR) {
        return VFS_ERR_INVALID_PATH;
    }
    if (node->first_child >= 0) {
        return VFS_ERR_IO;
    }

    memfs_reset_node(node_index);
    return VFS_OK;
}

static const VFSBackendOps g_memfs_backend_ops = {
    memfs_backend_list_files,
    memfs_backend_get_file_info,
    memfs_backend_read_file,
    memfs_backend_write_file,
    memfs_backend_touch_file,
    memfs_backend_delete_file,
    memfs_backend_mkdir,
    memfs_backend_rmdir,
};

static void vfs_reset_open_file(VFSOpenFile* file) {
    if (file == 0) {
        return;
    }

    file->active = 0;
    file->dirty = 0;
    file->mode = 0;
    file->owner_pid = 0;
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

int vfs_mount_fat12_root(FAT12Driver* fat12) {
    if (fat12 == 0) {
        return VFS_ERR_NOT_READY;
    }
    return vfs_mount_root("fat12", VFS_BACKEND_FAT12, &g_fat12_backend_ops, fat12);
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
    if (relative_path == 0) {
        relative_path = "";
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
        file->owner_pid = owner_pid;
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

        if (mode & VFS_OPEN_APPEND) {
            file->position = file->size;
        }

        return fd;
    }

    return VFS_ERR_NO_SLOT;
}

int vfs_open(const char* path, uint32_t mode) {
    return vfs_open_for_owner(path, mode, 0);
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

int vfs_seek(int fd, int32_t offset, uint32_t whence, uint32_t* position_out) {
    VFSOpenFile* file = vfs_get_open_file(fd);
    if (file == 0) {
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
    if (file == 0 || position_out == 0) {
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

    if (file->dirty) {
        int result = vfs_write_file(file->path, file->buffer, file->size);
        if (result != VFS_OK) {
            return result;
        }
    }

    vfs_reset_open_file(file);
    return VFS_OK;
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
