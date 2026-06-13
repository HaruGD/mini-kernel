#include "fs/vfs.h"

#include "heap.h"
#include "arch/x86/io.h"
#include "drivers/terminal.h"

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
    uint8_t kind;
    uint16_t mode;
    uint32_t owner_pid;
    uint32_t position;
    uint32_t size;
    uint32_t capacity;
    uint32_t dir_cursor;
    char path[VFS_PATH_MAX];
    uint8_t* buffer;
};

static VFSOpenFile g_vfs_open_files[VFS_MAX_OPEN_FILES];

enum VFSHandleKind {
    VFS_HANDLE_NONE = 0,
    VFS_HANDLE_FILE = 1,
    VFS_HANDLE_DIR = 2,
};

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

static void vfs_print_dir_entry(const VFSDirEntry* entry) {
    if (entry == 0) {
        return;
    }
    vfs_print("\n");
    vfs_print(entry->name);
    if (entry->type == VFS_NODE_DIR) {
        vfs_print("/");
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
