#ifndef VFS_H
#define VFS_H

#include <stdint.h>

class FAT12Driver;
class FAT32Driver;

enum VFSResult {
    VFS_OK = 0,
    VFS_ERR_NOT_READY = -1,
    VFS_ERR_INVALID_PATH = -2,
    VFS_ERR_NOT_FOUND = -3,
    VFS_ERR_BUFFER_TOO_SMALL = -4,
    VFS_ERR_IO = -5,
    VFS_ERR_ALREADY_MOUNTED = -6,
    VFS_ERR_NO_SLOT = -7,
    VFS_ERR_UNSUPPORTED = -8,
};

enum VFSBackendKind {
    VFS_BACKEND_NONE = 0,
    VFS_BACKEND_FAT12 = 1,
    VFS_BACKEND_MEMFS = 2,
    VFS_BACKEND_FAT32 = 3,
};

enum VFSOpenMode {
    VFS_OPEN_READ = 0x00000001,
    VFS_OPEN_WRITE = 0x00000002,
    VFS_OPEN_CREATE = 0x00000004,
    VFS_OPEN_TRUNCATE = 0x00000008,
    VFS_OPEN_APPEND = 0x00000010,
};

enum VFSSeekWhence {
    VFS_SEEK_SET = 0,
    VFS_SEEK_CUR = 1,
    VFS_SEEK_END = 2,
};

enum VFSNodeType {
    VFS_NODE_NONE = 0,
    VFS_NODE_FILE = 1,
    VFS_NODE_DIR = 2,
};

struct VFSFileInfo {
    uint32_t type;
    uint32_t size;
};

struct VFSDirEntry {
    uint32_t type;
    uint32_t size;
    char name[32];
};

struct VFSBackendOps {
    int (*list_files)(void* backend_ctx, const char* relative_path);
    int (*read_dir_entry)(void* backend_ctx, const char* relative_path, uint32_t cursor, VFSDirEntry* entry);
    int (*get_file_info)(void* backend_ctx, const char* relative_path, VFSFileInfo* info);
    int (*read_file)(void* backend_ctx, const char* relative_path, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read_out);
    int (*write_file)(void* backend_ctx, const char* relative_path, const uint8_t* buffer, uint32_t size);
    int (*touch_file)(void* backend_ctx, const char* relative_path);
    int (*delete_file)(void* backend_ctx, const char* relative_path);
    int (*mkdir)(void* backend_ctx, const char* relative_path);
    int (*rmdir)(void* backend_ctx, const char* relative_path);
};

struct VFSMountInfo {
    char mount_path[16];
    char fs_name[16];
    uint32_t backend_kind;
};

void vfs_init();
int vfs_mount(const char* mount_path, const char* fs_name, uint32_t backend_kind, const VFSBackendOps* ops, void* backend_ctx);
int vfs_mount_root(const char* fs_name, uint32_t backend_kind, const VFSBackendOps* ops, void* backend_ctx);
int vfs_mount_fat12_root(FAT12Driver* fat12);
int vfs_mount_fat32(const char* mount_path, FAT32Driver* fat32);
int vfs_mount_memfs(const char* mount_path);
uint32_t vfs_mount_count();
int vfs_get_mount_info(uint32_t index, VFSMountInfo* info);

int vfs_list_files();
int vfs_list_files_at(const char* path);
int vfs_get_file_info(const char* path, VFSFileInfo* info);
int vfs_read_file(const char* path, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read_out);
int vfs_write_file(const char* path, const uint8_t* buffer, uint32_t size);
int vfs_touch_file(const char* path);
int vfs_delete_file(const char* path);
int vfs_mkdir(const char* path);
int vfs_rmdir(const char* path);

int vfs_open(const char* path, uint32_t mode);
int vfs_open_for_owner(const char* path, uint32_t mode, uint32_t owner_pid);
int vfs_read(int fd, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read_out);
int vfs_write(int fd, const uint8_t* buffer, uint32_t size, uint32_t* bytes_written_out);
int vfs_seek(int fd, int32_t offset, uint32_t whence, uint32_t* position_out);
int vfs_tell(int fd, uint32_t* position_out);
int vfs_close(int fd);
int vfs_opendir(const char* path);
int vfs_opendir_for_owner(const char* path, uint32_t owner_pid);
int vfs_readdir(int fd, VFSDirEntry* entry);
int vfs_closedir(int fd);
uint32_t vfs_close_all_for_owner(uint32_t owner_pid);
uint32_t vfs_count_open_for_owner(uint32_t owner_pid);

#endif
