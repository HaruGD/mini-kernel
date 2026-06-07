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

static int fat32_is_dot_name(const FAT32DirEntry& entry) {
    if (entry.name[0] != '.') {
        return 0;
    }
    if (entry.name[1] == ' ') {
        return 1;
    }
    return entry.name[1] == '.' && entry.name[2] == ' ';
}

static uint8_t fat32_short_name_checksum(const uint8_t name[11]) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < 11; i++) {
        sum = (uint8_t)(((sum & 1u) ? 0x80u : 0u) + (sum >> 1) + name[i]);
    }
    return sum;
}

static void fat32_lfn_reset(char* buffer, uint32_t capacity) {
    if (buffer == 0 || capacity == 0) {
        return;
    }
    buffer[0] = '\0';
}

static void fat32_lfn_append_chunk(char* buffer,
                                   uint32_t capacity,
                                   const uint16_t* src,
                                   uint32_t count,
                                   uint32_t base_index) {
    if (buffer == 0 || capacity == 0 || src == 0) {
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        uint16_t value = src[i];
        if (value == 0x0000) {
            if (base_index + i < capacity) {
                buffer[base_index + i] = '\0';
            }
            break;
        }
        if (value == 0xFFFF) {
            break;
        }

        uint32_t out_index = base_index + i;
        if (out_index >= capacity) {
            break;
        }

        buffer[out_index] = (char)(value & 0x00FFu);
    }
}

static void fat32_lfn_collect(char* buffer, uint32_t capacity, const FAT32LFNEntry& lfn) {
    if (buffer == 0 || capacity == 0) {
        return;
    }

    uint8_t order = (uint8_t)(lfn.order & 0x1Fu);
    if (order == 0) {
        return;
    }

    uint16_t name1[5];
    uint16_t name2[6];
    uint16_t name3[2];
    for (uint32_t i = 0; i < 5; i++) {
        name1[i] = lfn.name1[i];
    }
    for (uint32_t i = 0; i < 6; i++) {
        name2[i] = lfn.name2[i];
    }
    for (uint32_t i = 0; i < 2; i++) {
        name3[i] = lfn.name3[i];
    }

    uint32_t base = (uint32_t)(order - 1u) * 13u;
    fat32_lfn_append_chunk(buffer, capacity, name1, 5, base);
    fat32_lfn_append_chunk(buffer, capacity, name2, 6, base + 5u);
    fat32_lfn_append_chunk(buffer, capacity, name3, 2, base + 11u);
}

static int fat32_name_eq_ci(const char* a, const char* b) {
    uint32_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (fat32_to_upper(a[i]) != fat32_to_upper(b[i])) {
            return 0;
        }
        i++;
    }
    return a[i] == '\0' && b[i] == '\0';
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

bool FAT32Driver::write_sector_relative(uint32_t lba, const uint8_t* buffer) {
    if (ata == 0 || buffer == 0) {
        return false;
    }
    return ata->write_sector(start_lba + lba, buffer);
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

bool FAT32Driver::write_fat_entry(uint32_t cluster, uint32_t value) {
    uint8_t sector[512];
    uint32_t fat_offset = cluster * 4u;
    uint32_t sector_index = fat_offset / bpb.bytes_per_sector;
    uint32_t sector_offset = fat_offset % bpb.bytes_per_sector;

    for (uint32_t fat_copy = 0; fat_copy < bpb.fat_count; fat_copy++) {
        uint32_t fat_lba = fat_start_lba + (fat_copy * bpb.sectors_per_fat_32) + sector_index;
        if (!read_sector_relative(fat_lba, sector)) {
            return false;
        }

        uint32_t current = *(uint32_t*)(sector + sector_offset);
        current &= 0xF0000000u;
        current |= (value & 0x0FFFFFFFu);
        *(uint32_t*)(sector + sector_offset) = current;

        if (!write_sector_relative(fat_lba, sector)) {
            return false;
        }
    }
    return true;
}

bool FAT32Driver::read_cluster_sector(uint32_t cluster, uint32_t sector_offset, uint8_t* buffer) {
    if (sector_offset >= bpb.sectors_per_cluster) {
        return false;
    }
    return read_sector_relative(cluster_to_lba(cluster) + sector_offset, buffer);
}

bool FAT32Driver::write_cluster_sector(uint32_t cluster, uint32_t sector_offset, const uint8_t* buffer) {
    if (sector_offset >= bpb.sectors_per_cluster) {
        return false;
    }
    return write_sector_relative(cluster_to_lba(cluster) + sector_offset, buffer);
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

int FAT32Driver::read_dir_entry_internal(uint32_t dir_cluster,
                                         uint32_t visible_index,
                                         FAT32DirEntry* entry,
                                         char* name_out,
                                         uint32_t name_capacity) {
    if (!initialized || entry == 0 || dir_cluster < 2u) {
        return VFS_ERR_INVALID_PATH;
    }

    uint8_t sector[512];
    uint32_t seen = 0;
    uint32_t cluster = dir_cluster;
    char lfn_name[64];
    fat32_lfn_reset(lfn_name, sizeof(lfn_name));
    uint8_t lfn_active = 0;
    uint8_t lfn_checksum = 0;

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
                    fat32_lfn_reset(lfn_name, sizeof(lfn_name));
                    lfn_active = 0;
                    continue;
                }
                if (dir[i].attributes == 0x0F) {
                    const FAT32LFNEntry* lfn = (const FAT32LFNEntry*)&dir[i];
                    if (lfn->order & 0x40u) {
                        fat32_lfn_reset(lfn_name, sizeof(lfn_name));
                        lfn_active = 1;
                        lfn_checksum = lfn->checksum;
                    } else if (!lfn_active || lfn->checksum != lfn_checksum) {
                        fat32_lfn_reset(lfn_name, sizeof(lfn_name));
                        lfn_active = 1;
                        lfn_checksum = lfn->checksum;
                    }
                    fat32_lfn_collect(lfn_name, sizeof(lfn_name), *lfn);
                    continue;
                }
                if (dir[i].attributes & 0x08) {
                    fat32_lfn_reset(lfn_name, sizeof(lfn_name));
                    lfn_active = 0;
                    continue;
                }
                if (fat32_is_dot_name(dir[i])) {
                    fat32_lfn_reset(lfn_name, sizeof(lfn_name));
                    lfn_active = 0;
                    continue;
                }

                if (seen == visible_index) {
                    fat32_copy_dir_entry(entry, &dir[i]);
                    if (name_out != 0 && name_capacity > 0) {
                        if (lfn_active && lfn_name[0] != '\0' &&
                            fat32_short_name_checksum(dir[i].name) == lfn_checksum) {
                            uint32_t j = 0;
                            while (lfn_name[j] != '\0' && j + 1 < name_capacity) {
                                name_out[j] = lfn_name[j];
                                j++;
                            }
                            name_out[j] = '\0';
                        } else if (!dir_entry_name(dir[i], name_out, name_capacity)) {
                            return VFS_ERR_IO;
                        }
                    }
                    return 1;
                }
                seen++;
                fat32_lfn_reset(lfn_name, sizeof(lfn_name));
                lfn_active = 0;
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

bool FAT32Driver::read_dir_sector(uint32_t dir_cluster, uint32_t sector_offset, uint8_t* sector) {
    return read_cluster_sector(dir_cluster, sector_offset, sector);
}

bool FAT32Driver::write_dir_sector(uint32_t dir_cluster, uint32_t sector_offset, const uint8_t* sector) {
    return write_cluster_sector(dir_cluster, sector_offset, sector);
}

int FAT32Driver::find_in_directory(uint32_t dir_cluster, const char* segment, FAT32DirEntry* entry) {
    if (segment == 0 || segment[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }

    FAT32DirEntry current;
    char current_name[64];
    for (uint32_t index = 0;; index++) {
        int result = read_dir_entry_internal(dir_cluster, index, &current, current_name, sizeof(current_name));
        if (result <= 0) {
            return result == 0 ? VFS_ERR_NOT_FOUND : result;
        }

        if (fat32_name_eq_ci(current_name, segment)) {
            if (entry != 0) {
                fat32_copy_dir_entry(entry, &current);
            }
            return VFS_OK;
        }
    }
}

int FAT32Driver::find_in_directory_with_location(uint32_t dir_cluster,
                                                 const char* segment,
                                                 FAT32DirEntry* entry,
                                                 uint32_t* out_dir_cluster,
                                                 uint32_t* out_sector_offset,
                                                 uint32_t* out_entry_index) {
    if (segment == 0 || segment[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }

    uint8_t sector[512];
    uint32_t cluster = dir_cluster;
    char lfn_name[64];
    fat32_lfn_reset(lfn_name, sizeof(lfn_name));
    uint8_t lfn_active = 0;
    uint8_t lfn_checksum = 0;

    while (cluster >= 2u && !is_end_of_chain(cluster)) {
        for (uint32_t sector_offset = 0; sector_offset < bpb.sectors_per_cluster; sector_offset++) {
            if (!read_dir_sector(cluster, sector_offset, sector)) {
                return VFS_ERR_IO;
            }

            FAT32DirEntry* dir = (FAT32DirEntry*)sector;
            uint32_t entry_count = bpb.bytes_per_sector / sizeof(FAT32DirEntry);
            for (uint32_t i = 0; i < entry_count; i++) {
                if (dir[i].name[0] == 0x00) {
                    return VFS_ERR_NOT_FOUND;
                }
                if (dir[i].name[0] == 0xE5) {
                    fat32_lfn_reset(lfn_name, sizeof(lfn_name));
                    lfn_active = 0;
                    continue;
                }
                if (dir[i].attributes == 0x0F) {
                    const FAT32LFNEntry* lfn = (const FAT32LFNEntry*)&dir[i];
                    if (lfn->order & 0x40u) {
                        fat32_lfn_reset(lfn_name, sizeof(lfn_name));
                        lfn_active = 1;
                        lfn_checksum = lfn->checksum;
                    } else if (!lfn_active || lfn->checksum != lfn_checksum) {
                        fat32_lfn_reset(lfn_name, sizeof(lfn_name));
                        lfn_active = 1;
                        lfn_checksum = lfn->checksum;
                    }
                    fat32_lfn_collect(lfn_name, sizeof(lfn_name), *lfn);
                    continue;
                }
                if ((dir[i].attributes & 0x08) || fat32_is_dot_name(dir[i])) {
                    fat32_lfn_reset(lfn_name, sizeof(lfn_name));
                    lfn_active = 0;
                    continue;
                }

                char current_name[64];
                if (lfn_active && lfn_name[0] != '\0' &&
                    fat32_short_name_checksum(dir[i].name) == lfn_checksum) {
                    uint32_t j = 0;
                    while (lfn_name[j] != '\0' && j + 1 < sizeof(current_name)) {
                        current_name[j] = lfn_name[j];
                        j++;
                    }
                    current_name[j] = '\0';
                } else if (!dir_entry_name(dir[i], current_name, sizeof(current_name))) {
                    return VFS_ERR_IO;
                }

                if (!fat32_name_eq_ci(current_name, segment)) {
                    fat32_lfn_reset(lfn_name, sizeof(lfn_name));
                    lfn_active = 0;
                    continue;
                }

                if (entry != 0) {
                    fat32_copy_dir_entry(entry, &dir[i]);
                }
                if (out_dir_cluster != 0) {
                    *out_dir_cluster = cluster;
                }
                if (out_sector_offset != 0) {
                    *out_sector_offset = sector_offset;
                }
                if (out_entry_index != 0) {
                    *out_entry_index = i;
                }
                return VFS_OK;
            }
        }

        uint32_t next = read_fat_entry(cluster);
        if (next == cluster) {
            return VFS_ERR_IO;
        }
        cluster = next;
    }

    return VFS_ERR_NOT_FOUND;
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

        char segment[64];
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

int FAT32Driver::resolve_parent_path(const char* path, uint32_t* out_dir_cluster, char out_leaf83[11]) {
    if (!initialized || path == 0 || path[0] == '\0' || out_dir_cluster == 0 || out_leaf83 == 0) {
        return VFS_ERR_INVALID_PATH;
    }

    const char* cursor = path;
    uint32_t current_cluster = bpb.root_cluster;
    FAT32DirEntry current_entry = {};

    while (*cursor == '/') {
        cursor++;
    }
    if (*cursor == '\0') {
        return VFS_ERR_INVALID_PATH;
    }

    while (*cursor != '\0') {
        char segment[64];
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

        const char* next = cursor + len;
        while (*next == '/') {
            next++;
        }

        if (*next == '\0') {
            to_name83(segment, out_leaf83);
            *out_dir_cluster = current_cluster;
            return VFS_OK;
        }

        int result = find_in_directory(current_cluster, segment, &current_entry);
        if (result != VFS_OK) {
            return result;
        }
        if ((current_entry.attributes & 0x10) == 0) {
            return VFS_ERR_NOT_FOUND;
        }

        current_cluster = dir_entry_cluster(current_entry);
        cursor = next;
    }

    return VFS_ERR_INVALID_PATH;
}

int FAT32Driver::find_free_dir_slot(uint32_t dir_cluster,
                                    uint32_t* out_dir_cluster,
                                    uint32_t* out_sector_offset,
                                    uint32_t* out_entry_index) {
    if (!initialized) {
        return VFS_ERR_NOT_READY;
    }

    uint8_t sector[512];
    uint32_t cluster = dir_cluster;
    uint32_t last_cluster = 0;

    while (cluster >= 2u && !is_end_of_chain(cluster)) {
        last_cluster = cluster;
        for (uint32_t sector_offset = 0; sector_offset < bpb.sectors_per_cluster; sector_offset++) {
            if (!read_dir_sector(cluster, sector_offset, sector)) {
                return VFS_ERR_IO;
            }

            FAT32DirEntry* dir = (FAT32DirEntry*)sector;
            uint32_t entry_count = bpb.bytes_per_sector / sizeof(FAT32DirEntry);
            for (uint32_t i = 0; i < entry_count; i++) {
                if (dir[i].name[0] == 0x00 || dir[i].name[0] == 0xE5) {
                    if (out_dir_cluster != 0) {
                        *out_dir_cluster = cluster;
                    }
                    if (out_sector_offset != 0) {
                        *out_sector_offset = sector_offset;
                    }
                    if (out_entry_index != 0) {
                        *out_entry_index = i;
                    }
                    return VFS_OK;
                }
            }
        }

        uint32_t next = read_fat_entry(cluster);
        if (next == cluster) {
            return VFS_ERR_IO;
        }
        cluster = next;
    }

    if (last_cluster < 2u) {
        return VFS_ERR_IO;
    }

    uint32_t new_cluster = 0;
    if (!alloc_free_cluster(&new_cluster)) {
        return VFS_ERR_IO;
    }

    for (uint32_t i = 0; i < bpb.bytes_per_sector; i++) {
        sector[i] = 0;
    }
    for (uint32_t sector_offset = 0; sector_offset < bpb.sectors_per_cluster; sector_offset++) {
        if (!write_cluster_sector(new_cluster, sector_offset, sector)) {
            free_cluster_chain(new_cluster);
            return VFS_ERR_IO;
        }
    }

    if (!write_fat_entry(last_cluster, new_cluster)) {
        free_cluster_chain(new_cluster);
        return VFS_ERR_IO;
    }

    if (out_dir_cluster != 0) {
        *out_dir_cluster = new_cluster;
    }
    if (out_sector_offset != 0) {
        *out_sector_offset = 0;
    }
    if (out_entry_index != 0) {
        *out_entry_index = 0;
    }
    return VFS_OK;
}

bool FAT32Driver::alloc_free_cluster(uint32_t* out_cluster) {
    if (!initialized || out_cluster == 0) {
        return false;
    }

    uint32_t data_sectors = total_sectors - data_start_lba;
    uint32_t cluster_count = data_sectors / bpb.sectors_per_cluster;
    uint32_t limit = cluster_count + 2u;

    for (uint32_t cluster = 2; cluster < limit; cluster++) {
        if (read_fat_entry(cluster) == 0) {
            if (!write_fat_entry(cluster, 0x0FFFFFFFu)) {
                return false;
            }
            *out_cluster = cluster;
            return true;
        }
    }
    return false;
}

bool FAT32Driver::free_cluster_chain(uint32_t start_cluster) {
    if (!initialized) {
        return false;
    }
    if (start_cluster < 2u) {
        return true;
    }

    uint32_t cluster = start_cluster;
    uint32_t guard = 0;
    while (cluster >= 2u && guard < 4096) {
        uint32_t next = read_fat_entry(cluster);
        if (!write_fat_entry(cluster, 0)) {
            return false;
        }
        if (next == cluster || next < 2u || is_end_of_chain(next)) {
            break;
        }
        cluster = next;
        guard++;
    }
    return true;
}

bool FAT32Driver::write_cluster_chain(const uint8_t* buffer, uint32_t size, uint32_t* out_first_cluster) {
    if (!initialized || out_first_cluster == 0) {
        return false;
    }

    if (size == 0) {
        *out_first_cluster = 0;
        return true;
    }

    uint32_t remaining = size;
    uint32_t offset = 0;
    uint32_t first_cluster = 0;
    uint32_t previous_cluster = 0;
    uint8_t sector[512];

    while (remaining > 0) {
        uint32_t cluster = 0;
        if (!alloc_free_cluster(&cluster)) {
            if (first_cluster >= 2u) {
                free_cluster_chain(first_cluster);
            }
            return false;
        }

        if (first_cluster == 0) {
            first_cluster = cluster;
        }
        if (previous_cluster >= 2u && !write_fat_entry(previous_cluster, cluster)) {
            free_cluster_chain(first_cluster);
            return false;
        }
        previous_cluster = cluster;

        for (uint32_t sector_offset = 0; sector_offset < bpb.sectors_per_cluster; sector_offset++) {
            for (uint32_t i = 0; i < bpb.bytes_per_sector; i++) {
                sector[i] = 0;
            }

            uint32_t chunk = remaining > bpb.bytes_per_sector ? bpb.bytes_per_sector : remaining;
            for (uint32_t i = 0; i < chunk; i++) {
                sector[i] = buffer[offset + i];
            }

            if (!write_cluster_sector(cluster, sector_offset, sector)) {
                free_cluster_chain(first_cluster);
                return false;
            }

            offset += chunk;
            remaining -= chunk;
            if (remaining == 0) {
                break;
            }
        }
    }

    *out_first_cluster = first_cluster;
    return true;
}

bool FAT32Driver::update_dir_entry(uint32_t dir_cluster, uint32_t sector_offset, uint32_t entry_index, const FAT32DirEntry* entry) {
    if (entry == 0) {
        return false;
    }

    uint8_t sector[512];
    if (!read_dir_sector(dir_cluster, sector_offset, sector)) {
        return false;
    }

    FAT32DirEntry* dir = (FAT32DirEntry*)sector;
    if (entry_index >= bpb.bytes_per_sector / sizeof(FAT32DirEntry)) {
        return false;
    }

    fat32_copy_dir_entry(&dir[entry_index], entry);
    return write_dir_sector(dir_cluster, sector_offset, sector);
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
    for (uint32_t index = 0;; index++) {
        VFSDirEntry dir_entry;
        int step = read_dir_entry(path != 0 ? path : "", index, &dir_entry);
        if (step < 0) {
            return step;
        }
        if (step == 0) {
            return VFS_OK;
        }

        terminal.print("\n");
        terminal.print(dir_entry.name);
        if (dir_entry.type == VFS_NODE_DIR) {
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
    int step = read_dir_entry_internal(dir_cluster, index, &dir_entry, entry->name, sizeof(entry->name));
    if (step <= 0) {
        return step;
    }

    entry->type = (dir_entry.attributes & 0x10) ? VFS_NODE_DIR : VFS_NODE_FILE;
    entry->size = dir_entry.file_size;
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

int FAT32Driver::write_file_path(const char* path, const uint8_t* buffer, uint32_t size) {
    uint32_t parent_cluster = 0;
    char leaf83[11];
    int result = resolve_parent_path(path, &parent_cluster, leaf83);
    if (result != VFS_OK) {
        return result;
    }

    FAT32DirEntry existing = {};
    uint32_t existing_dir_cluster = 0;
    uint32_t existing_sector_offset = 0;
    uint32_t existing_entry_index = 0;
    const char* leaf = path;
    if (path != 0) {
        const char* cursor = path;
        while (*cursor != '\0') {
            if (*cursor == '/') {
                leaf = cursor + 1;
            }
            cursor++;
        }
    }
    int found = find_in_directory_with_location(parent_cluster,
                                                leaf != 0 ? leaf : "",
                                                &existing,
                                                &existing_dir_cluster,
                                                &existing_sector_offset,
                                                &existing_entry_index);

    uint32_t target_dir_cluster = 0;
    uint32_t target_sector_offset = 0;
    uint32_t target_entry_index = 0;
    uint32_t old_cluster = 0;

    if (found == VFS_OK) {
        if ((existing.attributes & 0x10) != 0) {
            return VFS_ERR_INVALID_PATH;
        }
        target_dir_cluster = existing_dir_cluster;
        target_sector_offset = existing_sector_offset;
        target_entry_index = existing_entry_index;
        old_cluster = dir_entry_cluster(existing);
    } else if (found == VFS_ERR_NOT_FOUND) {
        result = find_free_dir_slot(parent_cluster, &target_dir_cluster, &target_sector_offset, &target_entry_index);
        if (result != VFS_OK) {
            return result;
        }
    } else {
        return found;
    }

    uint32_t first_cluster = 0;
    if (!write_cluster_chain(buffer, size, &first_cluster)) {
        return VFS_ERR_IO;
    }

    FAT32DirEntry new_entry = {};
    for (uint32_t i = 0; i < 11; i++) {
        new_entry.name[i] = (uint8_t)leaf83[i];
    }
    new_entry.attributes = 0x20;
    new_entry.first_cluster_high = (uint16_t)((first_cluster >> 16) & 0xFFFF);
    new_entry.first_cluster_low = (uint16_t)(first_cluster & 0xFFFF);
    new_entry.file_size = size;

    if (!update_dir_entry(target_dir_cluster, target_sector_offset, target_entry_index, &new_entry)) {
        if (first_cluster >= 2u) {
            free_cluster_chain(first_cluster);
        }
        return VFS_ERR_IO;
    }

    if (old_cluster >= 2u) {
        if (!free_cluster_chain(old_cluster)) {
            return VFS_ERR_IO;
        }
    }

    return VFS_OK;
}

int FAT32Driver::touch_file_path(const char* path) {
    return write_file_path(path, 0, 0);
}

int FAT32Driver::delete_file_path(const char* path) {
    uint32_t parent_cluster = 0;
    char leaf83[11];
    int result = resolve_parent_path(path, &parent_cluster, leaf83);
    if (result != VFS_OK) {
        return result;
    }

    FAT32DirEntry existing = {};
    uint32_t dir_cluster = 0;
    uint32_t sector_offset = 0;
    uint32_t entry_index = 0;
    const char* leaf = path;
    if (path != 0) {
        const char* slash = path;
        const char* last = path;
        while (*slash != '\0') {
            if (*slash == '/') {
                last = slash + 1;
            }
            slash++;
        }
        leaf = last;
    }
    result = find_in_directory_with_location(parent_cluster,
                                             leaf,
                                             &existing,
                                             &dir_cluster,
                                             &sector_offset,
                                             &entry_index);
    if (result != VFS_OK) {
        return result;
    }
    if ((existing.attributes & 0x10) != 0) {
        return VFS_ERR_INVALID_PATH;
    }

    uint8_t sector[512];
    if (!read_dir_sector(dir_cluster, sector_offset, sector)) {
        return VFS_ERR_IO;
    }
    FAT32DirEntry* dir = (FAT32DirEntry*)sector;
    dir[entry_index].name[0] = 0xE5;
    if (!write_dir_sector(dir_cluster, sector_offset, sector)) {
        return VFS_ERR_IO;
    }

    uint32_t first_cluster = dir_entry_cluster(existing);
    if (first_cluster >= 2u && !free_cluster_chain(first_cluster)) {
        return VFS_ERR_IO;
    }
    return VFS_OK;
}

int FAT32Driver::mkdir_path(const char* path) {
    uint32_t parent_cluster = 0;
    char leaf83[11];
    int result = resolve_parent_path(path, &parent_cluster, leaf83);
    if (result != VFS_OK) {
        return result;
    }

    const char* leaf = path;
    if (path != 0) {
        const char* cursor = path;
        while (*cursor != '\0') {
            if (*cursor == '/') {
                leaf = cursor + 1;
            }
            cursor++;
        }
    }

    FAT32DirEntry existing = {};
    result = find_in_directory(parent_cluster, leaf != 0 ? leaf : "", &existing);
    if (result == VFS_OK) {
        return VFS_ERR_ALREADY_MOUNTED;
    }
    if (result != VFS_ERR_NOT_FOUND) {
        return result;
    }

    uint32_t target_dir_cluster = 0;
    uint32_t target_sector_offset = 0;
    uint32_t target_entry_index = 0;
    result = find_free_dir_slot(parent_cluster, &target_dir_cluster, &target_sector_offset, &target_entry_index);
    if (result != VFS_OK) {
        return result;
    }

    uint32_t new_cluster = 0;
    if (!alloc_free_cluster(&new_cluster)) {
        return VFS_ERR_IO;
    }

    uint8_t sector[512];
    for (uint32_t sector_offset = 0; sector_offset < bpb.sectors_per_cluster; sector_offset++) {
        for (uint32_t i = 0; i < bpb.bytes_per_sector; i++) {
            sector[i] = 0;
        }

        if (sector_offset == 0) {
            FAT32DirEntry* dir = (FAT32DirEntry*)sector;
            for (uint32_t i = 0; i < 11; i++) {
                dir[0].name[i] = ' ';
                dir[1].name[i] = ' ';
            }
            dir[0].name[0] = '.';
            dir[0].attributes = 0x10;
            dir[0].first_cluster_high = (uint16_t)((new_cluster >> 16) & 0xFFFF);
            dir[0].first_cluster_low = (uint16_t)(new_cluster & 0xFFFF);

            dir[1].name[0] = '.';
            dir[1].name[1] = '.';
            dir[1].attributes = 0x10;
            dir[1].first_cluster_high = (uint16_t)((parent_cluster >> 16) & 0xFFFF);
            dir[1].first_cluster_low = (uint16_t)(parent_cluster & 0xFFFF);
        }

        if (!write_cluster_sector(new_cluster, sector_offset, sector)) {
            free_cluster_chain(new_cluster);
            return VFS_ERR_IO;
        }
    }

    FAT32DirEntry new_entry = {};
    for (uint32_t i = 0; i < 11; i++) {
        new_entry.name[i] = (uint8_t)leaf83[i];
    }
    new_entry.attributes = 0x10;
    new_entry.first_cluster_high = (uint16_t)((new_cluster >> 16) & 0xFFFF);
    new_entry.first_cluster_low = (uint16_t)(new_cluster & 0xFFFF);
    new_entry.file_size = 0;

    if (!update_dir_entry(target_dir_cluster, target_sector_offset, target_entry_index, &new_entry)) {
        free_cluster_chain(new_cluster);
        return VFS_ERR_IO;
    }

    return VFS_OK;
}

int FAT32Driver::rmdir_path(const char* path) {
    uint32_t parent_cluster = 0;
    char leaf83[11];
    int result = resolve_parent_path(path, &parent_cluster, leaf83);
    if (result != VFS_OK) {
        return result;
    }

    const char* leaf = path;
    if (path != 0) {
        const char* cursor = path;
        while (*cursor != '\0') {
            if (*cursor == '/') {
                leaf = cursor + 1;
            }
            cursor++;
        }
    }

    FAT32DirEntry existing = {};
    uint32_t dir_cluster = 0;
    uint32_t sector_offset = 0;
    uint32_t entry_index = 0;
    result = find_in_directory_with_location(parent_cluster,
                                             leaf != 0 ? leaf : "",
                                             &existing,
                                             &dir_cluster,
                                             &sector_offset,
                                             &entry_index);
    if (result != VFS_OK) {
        return result;
    }
    if ((existing.attributes & 0x10) == 0) {
        return VFS_ERR_INVALID_PATH;
    }

    uint32_t target_cluster = dir_entry_cluster(existing);
    if (target_cluster < 2u) {
        return VFS_ERR_INVALID_PATH;
    }

    uint8_t sector[512];
    uint32_t cluster = target_cluster;
    while (cluster >= 2u && !is_end_of_chain(cluster)) {
        for (uint32_t child_sector = 0; child_sector < bpb.sectors_per_cluster; child_sector++) {
            if (!read_dir_sector(cluster, child_sector, sector)) {
                return VFS_ERR_IO;
            }

            FAT32DirEntry* dir = (FAT32DirEntry*)sector;
            uint32_t count = bpb.bytes_per_sector / sizeof(FAT32DirEntry);
            for (uint32_t i = 0; i < count; i++) {
                if (dir[i].name[0] == 0x00) {
                    break;
                }
                if (dir[i].name[0] == 0xE5 || dir[i].attributes == 0x0F || (dir[i].attributes & 0x08)) {
                    continue;
                }
                if (fat32_is_dot_name(dir[i])) {
                    continue;
                }
                return VFS_ERR_IO;
            }
        }

        uint32_t next = read_fat_entry(cluster);
        if (next == cluster) {
            return VFS_ERR_IO;
        }
        if (next < 2u || is_end_of_chain(next)) {
            break;
        }
        cluster = next;
    }

    if (!read_dir_sector(dir_cluster, sector_offset, sector)) {
        return VFS_ERR_IO;
    }
    FAT32DirEntry* dir = (FAT32DirEntry*)sector;
    dir[entry_index].name[0] = 0xE5;
    if (!write_dir_sector(dir_cluster, sector_offset, sector)) {
        return VFS_ERR_IO;
    }

    if (!free_cluster_chain(target_cluster)) {
        return VFS_ERR_IO;
    }
    return VFS_OK;
}
