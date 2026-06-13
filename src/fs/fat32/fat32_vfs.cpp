#include "fs/fat32.h"
#include "fs/vfs.h"

static int fat32_backend_list_files(void* backend_ctx, const char* relative_path) {
    FAT32Driver* fat32 = (FAT32Driver*)backend_ctx;
    if (fat32 == 0 || !fat32->ready()) {
        return VFS_ERR_NOT_READY;
    }
    return fat32->list_dir(relative_path != 0 ? relative_path : "");
}

static int fat32_backend_read_dir_entry(void* backend_ctx, const char* relative_path, uint32_t cursor, VFSDirEntry* entry) {
    FAT32Driver* fat32 = (FAT32Driver*)backend_ctx;
    if (fat32 == 0 || !fat32->ready()) {
        return VFS_ERR_NOT_READY;
    }
    return fat32->read_dir_entry(relative_path != 0 ? relative_path : "", cursor, entry);
}

static int fat32_backend_get_file_info(void* backend_ctx, const char* relative_path, VFSFileInfo* info) {
    FAT32Driver* fat32 = (FAT32Driver*)backend_ctx;
    if (fat32 == 0 || !fat32->ready()) {
        return VFS_ERR_NOT_READY;
    }
    return fat32->get_path_info(relative_path != 0 ? relative_path : "", info);
}

static int fat32_backend_read_file(void* backend_ctx,
                                   const char* relative_path,
                                   uint8_t* buffer,
                                   uint32_t buffer_size,
                                   uint32_t* bytes_read_out) {
    FAT32Driver* fat32 = (FAT32Driver*)backend_ctx;
    if (fat32 == 0 || !fat32->ready()) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    return fat32->read_file_path(relative_path, buffer, buffer_size, bytes_read_out);
}

static int fat32_backend_write_file(void* backend_ctx,
                                    const char* relative_path,
                                    const uint8_t* buffer,
                                    uint32_t size) {
    FAT32Driver* fat32 = (FAT32Driver*)backend_ctx;
    if (fat32 == 0 || !fat32->ready()) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    if (size > 0 && buffer == 0) {
        return VFS_ERR_IO;
    }
    return fat32->write_file_path(relative_path, buffer, size);
}

static int fat32_backend_touch_file(void* backend_ctx, const char* relative_path) {
    FAT32Driver* fat32 = (FAT32Driver*)backend_ctx;
    if (fat32 == 0 || !fat32->ready()) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    return fat32->touch_file_path(relative_path);
}

static int fat32_backend_delete_file(void* backend_ctx, const char* relative_path) {
    FAT32Driver* fat32 = (FAT32Driver*)backend_ctx;
    if (fat32 == 0 || !fat32->ready()) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    return fat32->delete_file_path(relative_path);
}

static int fat32_backend_mkdir(void* backend_ctx, const char* relative_path) {
    FAT32Driver* fat32 = (FAT32Driver*)backend_ctx;
    if (fat32 == 0 || !fat32->ready()) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    return fat32->mkdir_path(relative_path);
}

static int fat32_backend_rmdir(void* backend_ctx, const char* relative_path) {
    FAT32Driver* fat32 = (FAT32Driver*)backend_ctx;
    if (fat32 == 0 || !fat32->ready()) {
        return VFS_ERR_NOT_READY;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    return fat32->rmdir_path(relative_path);
}

static int fat32_backend_rename_path(void* backend_ctx,
                                     const char* old_relative_path,
                                     const char* new_relative_path) {
    FAT32Driver* fat32 = (FAT32Driver*)backend_ctx;
    if (fat32 == 0 || !fat32->ready()) {
        return VFS_ERR_NOT_READY;
    }
    if (old_relative_path == 0 || old_relative_path[0] == '\0' ||
        new_relative_path == 0 || new_relative_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }
    return fat32->rename_path(old_relative_path, new_relative_path);
}

static const VFSBackendOps g_fat32_backend_ops = {
    fat32_backend_list_files,
    fat32_backend_read_dir_entry,
    fat32_backend_get_file_info,
    fat32_backend_read_file,
    fat32_backend_write_file,
    fat32_backend_touch_file,
    fat32_backend_delete_file,
    fat32_backend_mkdir,
    fat32_backend_rmdir,
    fat32_backend_rename_path,
};

int vfs_mount_fat32_root(FAT32Driver* fat32) {
    if (fat32 == 0 || !fat32->ready()) {
        return VFS_ERR_NOT_READY;
    }
    return vfs_mount_root("fat32", VFS_BACKEND_FAT32, &g_fat32_backend_ops, fat32);
}

int vfs_mount_fat32(const char* mount_path, FAT32Driver* fat32) {
    if (fat32 == 0 || !fat32->ready()) {
        return VFS_ERR_NOT_READY;
    }
    return vfs_mount(mount_path, "fat32", VFS_BACKEND_FAT32, &g_fat32_backend_ops, fat32);
}
