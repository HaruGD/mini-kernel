#include "fs/fat32.h"

#include "drivers/terminal.h"

extern Terminal terminal;
static char fat32_to_upper(char ch) {
    if (ch >= 'a' && ch <= 'z') {
        return (char)(ch - ('a' - 'A'));
    }
    return ch;
}

static int fat32_str_eq(const char* a, const char* b) {
    uint32_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
}

static void fat32_copy_dir_entry(FAT32DirEntry* dest, const FAT32DirEntry* src) {
    if (dest == 0 || src == 0) {
        return;
    }
    for (uint32_t i = 0; i < sizeof(FAT32DirEntry); i++) {
        ((uint8_t*)dest)[i] = ((const uint8_t*)src)[i];
    }
}

FAT32Driver::FAT32Driver(ATADriver* ata, uint32_t start_lba)
    : ata(ata),
      start_lba(start_lba),
      initialized(0),
      fat_start_lba(0),
      data_start_lba(0),
      total_sectors(0) {
}

bool FAT32Driver::read_sector_relative(uint32_t lba, uint8_t* buffer) {
    if (ata == 0 || buffer == 0) {
        return false;
    }
    return ata->read_sector(start_lba + lba, buffer);
}

uint32_t FAT32Driver::cluster_to_lba(uint32_t cluster) const {
    return data_start_lba + ((cluster - 2u) * bpb.sectors_per_cluster);
}

uint32_t FAT32Driver::read_fat_entry(uint32_t cluster) {
    uint8_t sector[512];
    uint32_t fat_offset = cluster * 4u;
    uint32_t sector_index = fat_offset / bpb.bytes_per_sector;
    uint32_t sector_offset = fat_offset % bpb.bytes_per_sector;
    if (!read_sector_relative(fat_start_lba + sector_index, sector)) {
        return 0x0FFFFFF7u;
    }

    uint32_t value = *(uint32_t*)(sector + sector_offset);
    return value & 0x0FFFFFFFu;
}

bool FAT32Driver::read_cluster_sector(uint32_t cluster, uint32_t sector_offset, uint8_t* buffer) {
    if (sector_offset >= bpb.sectors_per_cluster) {
        return false;
    }
    return read_sector_relative(cluster_to_lba(cluster) + sector_offset, buffer);
}

bool FAT32Driver::is_end_of_chain(uint32_t value) const {
    return value >= 0x0FFFFFF8u;
}

void FAT32Driver::to_name83(const char* path_segment, char out[11]) const {
    for (int i = 0; i < 11; i++) {
        out[i] = ' ';
    }
    if (path_segment == 0) {
        return;
    }

    uint32_t source = 0;
    uint32_t target = 0;
    while (path_segment[source] != '\0' && path_segment[source] != '.' && target < 8) {
        out[target++] = (char)fat32_to_upper(path_segment[source++]);
    }
    if (path_segment[source] == '.') {
        source++;
        target = 8;
        while (path_segment[source] != '\0' && target < 11) {
            out[target++] = (char)fat32_to_upper(path_segment[source++]);
        }
    }
}

bool FAT32Driver::dir_entry_name(const FAT32DirEntry& entry, char* out, uint32_t capacity) const {
    if (out == 0 || capacity < 2) {
        return false;
    }

    uint32_t n = 0;
    for (uint32_t j = 0; j < 8 && entry.name[j] != ' '; j++) {
        if (n + 1 >= capacity) {
            return false;
        }
        out[n++] = (char)entry.name[j];
    }

    int has_ext = 0;
    for (uint32_t j = 8; j < 11; j++) {
        if (entry.name[j] != ' ') {
            has_ext = 1;
            break;
        }
    }

    if (has_ext) {
        if (n + 1 >= capacity) {
            return false;
        }
        out[n++] = '.';
        for (uint32_t j = 8; j < 11 && entry.name[j] != ' '; j++) {
            if (n + 1 >= capacity) {
                return false;
            }
            out[n++] = (char)entry.name[j];
        }
    }

    out[n] = '\0';
    return true;
}

uint32_t FAT32Driver::dir_entry_cluster(const FAT32DirEntry& entry) const {
    return ((uint32_t)entry.first_cluster_high << 16) | (uint32_t)entry.first_cluster_low;
}

int FAT32Driver::read_dir_entry_internal(uint32_t dir_cluster, uint32_t visible_index, FAT32DirEntry* entry) {
    if (!initialized || entry == 0 || dir_cluster < 2u) {
        return VFS_ERR_INVALID_PATH;
    }

    uint8_t sector[512];
    uint32_t seen = 0;
    uint32_t cluster = dir_cluster;

    while (cluster >= 2u && !is_end_of_chain(cluster)) {
        for (uint32_t sector_offset = 0; sector_offset < bpb.sectors_per_cluster; sector_offset++) {
            if (!read_cluster_sector(cluster, sector_offset, sector)) {
                return VFS_ERR_IO;
            }

            FAT32DirEntry* dir = (FAT32DirEntry*)sector;
            uint32_t entry_count = bpb.bytes_per_sector / sizeof(FAT32DirEntry);
            for (uint32_t i = 0; i < entry_count; i++) {
                if (dir[i].name[0] == 0x00) {
                    return 0;
                }
                if (dir[i].name[0] == 0xE5) {
                    continue;
                }
                if (dir[i].attributes == 0x0F) {
                    continue;
                }
                if (dir[i].attributes & 0x08) {
                    continue;
                }

                if (seen == visible_index) {
                    fat32_copy_dir_entry(entry, &dir[i]);
                    return 1;
                }
                seen++;
            }
        }

        uint32_t next = read_fat_entry(cluster);
        if (next == cluster) {
            return VFS_ERR_IO;
        }
        cluster = next;
    }

    return 0;
}

int FAT32Driver::find_in_directory(uint32_t dir_cluster, const char* segment, FAT32DirEntry* entry) {
    if (segment == 0 || segment[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }

    char target83[11];
    to_name83(segment, target83);

    FAT32DirEntry current;
    for (uint32_t index = 0;; index++) {
        int result = read_dir_entry_internal(dir_cluster, index, &current);
        if (result <= 0) {
            return result == 0 ? VFS_ERR_NOT_FOUND : result;
        }

        int match = 1;
        for (uint32_t j = 0; j < 11; j++) {
            if ((char)current.name[j] != target83[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            if (entry != 0) {
                fat32_copy_dir_entry(entry, &current);
            }
            return VFS_OK;
        }
    }
}

int FAT32Driver::resolve_path(const char* path, FAT32DirEntry* entry, uint32_t* out_cluster, uint8_t* out_is_root_dir) {
    if (!initialized) {
        return VFS_ERR_NOT_READY;
    }
    if (path == 0 || path[0] == '\0') {
        if (out_cluster != 0) {
            *out_cluster = bpb.root_cluster;
        }
        if (out_is_root_dir != 0) {
            *out_is_root_dir = 1;
        }
        return VFS_OK;
    }

    const char* cursor = path;
    uint32_t current_cluster = bpb.root_cluster;
    FAT32DirEntry current_entry = {};
    uint8_t found_any = 0;

    while (*cursor != '\0') {
        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        char segment[16];
        uint32_t len = 0;
        while (cursor[len] != '\0' && cursor[len] != '/') {
            if (len + 1 >= sizeof(segment)) {
                return VFS_ERR_INVALID_PATH;
            }
            segment[len] = cursor[len];
            len++;
        }
        segment[len] = '\0';

        if (fat32_str_eq(segment, ".") || fat32_str_eq(segment, "..")) {
            return VFS_ERR_UNSUPPORTED;
        }

        int result = find_in_directory(current_cluster, segment, &current_entry);
        if (result != VFS_OK) {
            return result;
        }

        found_any = 1;
        current_cluster = dir_entry_cluster(current_entry);
        cursor += len;

        while (*cursor == '/') {
            cursor++;
        }
        if (*cursor != '\0' && (current_entry.attributes & 0x10) == 0) {
            return VFS_ERR_NOT_FOUND;
        }
    }

    if (!found_any) {
        if (out_cluster != 0) {
            *out_cluster = bpb.root_cluster;
        }
        if (out_is_root_dir != 0) {
            *out_is_root_dir = 1;
        }
        return VFS_OK;
    }

    if (entry != 0) {
        fat32_copy_dir_entry(entry, &current_entry);
    }
    if (out_cluster != 0) {
        *out_cluster = current_cluster;
    }
    if (out_is_root_dir != 0) {
        *out_is_root_dir = 0;
    }
    return VFS_OK;
}

void FAT32Driver::init() {
    initialized = 0;
    if (ata == 0) {
        return;
    }

    uint8_t sector[512];
    if (!read_sector_relative(0, sector)) {
        return;
    }

    for (uint32_t i = 0; i < sizeof(FAT32BPB); i++) {
        ((uint8_t*)&bpb)[i] = sector[i];
    }

    if (bpb.bytes_per_sector != 512) {
        return;
    }
    if (bpb.sectors_per_cluster == 0) {
        return;
    }
    if (bpb.root_entry_count != 0) {
        return;
    }
    if (bpb.sectors_per_fat_16 != 0) {
        return;
    }
    if (bpb.sectors_per_fat_32 == 0) {
        return;
    }
    if (bpb.fat_count == 0) {
        return;
    }
    if (bpb.root_cluster < 2u) {
        return;
    }

    total_sectors = bpb.total_sectors_16 != 0 ? bpb.total_sectors_16 : bpb.total_sectors_32;
    fat_start_lba = bpb.reserved_sectors;
    data_start_lba = fat_start_lba + ((uint32_t)bpb.fat_count * bpb.sectors_per_fat_32);
    initialized = 1;
}

bool FAT32Driver::ready() const {
    return initialized != 0;
}

int FAT32Driver::list_dir(const char* path) {
    uint32_t dir_cluster = 0;
    uint8_t is_root = 0;
    FAT32DirEntry entry;
    int result = resolve_path(path != 0 ? path : "", &entry, &dir_cluster, &is_root);
    if (result != VFS_OK) {
        return result;
    }
    if (!is_root && (entry.attributes & 0x10) == 0) {
        return VFS_ERR_INVALID_PATH;
    }

    for (uint32_t index = 0;; index++) {
        FAT32DirEntry dir_entry;
        int step = read_dir_entry_internal(dir_cluster, index, &dir_entry);
        if (step < 0) {
            return step;
        }
        if (step == 0) {
            return VFS_OK;
        }

        char name[32];
        if (!dir_entry_name(dir_entry, name, sizeof(name))) {
            return VFS_ERR_IO;
        }
        terminal.print("\n");
        terminal.print(name);
        if (dir_entry.attributes & 0x10) {
            terminal.print("/");
        }
    }
}

int FAT32Driver::read_dir_entry(const char* path, uint32_t index, VFSDirEntry* entry) {
    uint32_t dir_cluster = 0;
    uint8_t is_root = 0;
    FAT32DirEntry dir_info;
    int result = resolve_path(path != 0 ? path : "", &dir_info, &dir_cluster, &is_root);
    if (result != VFS_OK) {
        return result;
    }
    if (!is_root && (dir_info.attributes & 0x10) == 0) {
        return VFS_ERR_INVALID_PATH;
    }

    FAT32DirEntry dir_entry;
    int step = read_dir_entry_internal(dir_cluster, index, &dir_entry);
    if (step <= 0) {
        return step;
    }

    entry->type = (dir_entry.attributes & 0x10) ? VFS_NODE_DIR : VFS_NODE_FILE;
    entry->size = dir_entry.file_size;
    if (!dir_entry_name(dir_entry, entry->name, sizeof(entry->name))) {
        return VFS_ERR_IO;
    }
    return 1;
}

int FAT32Driver::get_path_info(const char* path, VFSFileInfo* info) {
    uint32_t cluster = 0;
    uint8_t is_root = 0;
    FAT32DirEntry entry;
    int result = resolve_path(path != 0 ? path : "", &entry, &cluster, &is_root);
    if (result != VFS_OK) {
        return result;
    }

    if (info != 0) {
        if (is_root) {
            info->type = VFS_NODE_DIR;
            info->size = 0;
        } else {
            info->type = (entry.attributes & 0x10) ? VFS_NODE_DIR : VFS_NODE_FILE;
            info->size = (entry.attributes & 0x10) ? 0 : entry.file_size;
        }
    }
    return VFS_OK;
}

int FAT32Driver::read_file_path(const char* path, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read_out) {
    uint32_t file_cluster = 0;
    uint8_t is_root = 0;
    FAT32DirEntry entry;
    int result = resolve_path(path, &entry, &file_cluster, &is_root);
    if (result != VFS_OK) {
        return result;
    }
    if (is_root || (entry.attributes & 0x10) != 0) {
        return VFS_ERR_INVALID_PATH;
    }
    if (buffer == 0 || buffer_size < (entry.file_size > 0 ? entry.file_size : 1)) {
        return VFS_ERR_BUFFER_TOO_SMALL;
    }

    uint32_t remaining = entry.file_size;
    uint32_t copied = 0;
    uint32_t cluster = file_cluster;
    uint8_t sector[512];

    while (cluster >= 2u && !is_end_of_chain(cluster) && remaining > 0) {
        for (uint32_t sector_offset = 0; sector_offset < bpb.sectors_per_cluster && remaining > 0; sector_offset++) {
            if (!read_cluster_sector(cluster, sector_offset, sector)) {
                return VFS_ERR_IO;
            }
            uint32_t chunk = remaining > bpb.bytes_per_sector ? bpb.bytes_per_sector : remaining;
            for (uint32_t i = 0; i < chunk; i++) {
                buffer[copied + i] = sector[i];
            }
            copied += chunk;
            remaining -= chunk;
        }

        uint32_t next = read_fat_entry(cluster);
        if (next == cluster) {
            return VFS_ERR_IO;
        }
        cluster = next;
    }

    if (bytes_read_out != 0) {
        *bytes_read_out = copied;
    }
    return remaining == 0 ? VFS_OK : VFS_ERR_IO;
}
