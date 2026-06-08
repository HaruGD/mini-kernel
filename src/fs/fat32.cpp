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

static int fat32_is_alnum_ascii(char ch) {
    return (ch >= 'A' && ch <= 'Z') ||
           (ch >= 'a' && ch <= 'z') ||
           (ch >= '0' && ch <= '9');
}

static char fat32_sanitize_short_char(char ch) {
    ch = fat32_to_upper(ch);
    if (fat32_is_alnum_ascii(ch)) {
        return ch;
    }
    switch (ch) {
        case '_':
        case '$':
        case '~':
        case '!':
        case '#':
        case '%':
        case '-':
        case '{':
        case '}':
        case '(':
        case ')':
        case '@':
        case '^':
        case '&':
            return ch;
        default:
            return '_';
    }
}

static int fat32_is_simple_83_name(const char* name) {
    uint32_t base_len = 0;
    uint32_t ext_len = 0;
    uint8_t saw_dot = 0;

    if (name == 0 || name[0] == '\0') {
        return 0;
    }

    for (uint32_t i = 0; name[i] != '\0'; i++) {
        char ch = name[i];
        if (ch == '.') {
            if (saw_dot || base_len == 0) {
                return 0;
            }
            saw_dot = 1;
            continue;
        }
        if (ch == ' ' || ch == '/' || ch == '\\') {
            return 0;
        }
        if (fat32_to_upper(ch) != ch) {
            return 0;
        }
        if (!fat32_is_alnum_ascii(ch) && ch != '_' && ch != '$' && ch != '~' &&
            ch != '!' && ch != '#' && ch != '%' && ch != '-' && ch != '{' &&
            ch != '}' && ch != '(' && ch != ')' && ch != '@' && ch != '^' &&
            ch != '&') {
            return 0;
        }

        if (!saw_dot) {
            if (++base_len > 8) {
                return 0;
            }
        } else {
            if (++ext_len > 3) {
                return 0;
            }
        }
    }

    return base_len > 0;
}

static uint32_t fat32_lfn_slot_count(const char* long_name) {
    uint32_t len = 0;
    while (long_name != 0 && long_name[len] != '\0') {
        len++;
    }
    return (len + 12u) / 13u;
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

int FAT32Driver::find_short_name_in_directory(uint32_t dir_cluster, const char short_name[11], FAT32DirEntry* entry) {
    uint8_t sector[512];
    uint32_t cluster = dir_cluster;

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
                if (dir[i].name[0] == 0xE5 || dir[i].attributes == 0x0F || (dir[i].attributes & 0x08)) {
                    continue;
                }

                uint8_t matched = 1;
                for (uint32_t j = 0; j < 11; j++) {
                    if (dir[i].name[j] != (uint8_t)short_name[j]) {
                        matched = 0;
                        break;
                    }
                }
                if (!matched) {
                    continue;
                }

                if (entry != 0) {
                    fat32_copy_dir_entry(entry, &dir[i]);
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

static void fat32_build_short_alias_name(const char* leaf_name, uint32_t serial, char out[11]) {
    char base[8];
    char ext[3];
    uint32_t base_len = 0;
    uint32_t ext_len = 0;
    int32_t dot_index = -1;

    for (uint32_t i = 0; leaf_name != 0 && leaf_name[i] != '\0'; i++) {
        if (leaf_name[i] == '.') {
            dot_index = (int32_t)i;
        }
    }

    for (uint32_t i = 0; i < 11; i++) {
        out[i] = ' ';
    }

    if (dot_index >= 0) {
        for (uint32_t i = (uint32_t)dot_index + 1; leaf_name[i] != '\0' && ext_len < 3; i++) {
            ext[ext_len++] = fat32_sanitize_short_char(leaf_name[i]);
        }
    }

    uint32_t suffix_digits = serial >= 10 ? 2u : 1u;
    uint32_t prefix_limit = suffix_digits == 1 ? 6u : 5u;
    for (uint32_t i = 0; leaf_name[i] != '\0' && leaf_name[i] != '.' && base_len < prefix_limit; i++) {
        base[base_len++] = fat32_sanitize_short_char(leaf_name[i]);
    }
    if (base_len == 0) {
        base[base_len++] = 'X';
    }

    for (uint32_t i = 0; i < base_len; i++) {
        out[i] = base[i];
    }
    out[base_len] = '~';
    if (serial >= 10) {
        out[base_len + 1] = (char)('0' + ((serial / 10u) % 10u));
        out[base_len + 2] = (char)('0' + (serial % 10u));
    } else {
        out[base_len + 1] = (char)('0' + serial);
    }

    for (uint32_t i = 0; i < ext_len; i++) {
        out[8 + i] = ext[i];
    }
}

static void fat32_make_lfn_entry(const char* long_name,
                                 uint32_t long_len,
                                 uint32_t chunk_index,
                                 uint32_t chunk_count,
                                 uint8_t checksum,
                                 FAT32LFNEntry* entry) {
    uint16_t chars[13];
    uint32_t start = chunk_index * 13u;
    uint32_t copied = 0;

    for (uint32_t i = 0; i < 13; i++) {
        uint32_t pos = start + i;
        if (pos < long_len) {
            chars[i] = (uint8_t)long_name[pos];
            copied++;
        } else if (pos == long_len) {
            chars[i] = 0x0000u;
        } else {
            chars[i] = 0xFFFFu;
        }
    }

    entry->order = (uint8_t)(chunk_index + 1u);
    if (chunk_index + 1u == chunk_count) {
        entry->order |= 0x40u;
    }
    entry->attributes = 0x0Fu;
    entry->entry_type = 0;
    entry->checksum = checksum;
    entry->first_cluster_low = 0;
    for (uint32_t i = 0; i < 5; i++) {
        entry->name1[i] = chars[i];
    }
    for (uint32_t i = 0; i < 6; i++) {
        entry->name2[i] = chars[5u + i];
    }
    for (uint32_t i = 0; i < 2; i++) {
        entry->name3[i] = chars[11u + i];
    }
    (void)copied;
}

static const char* fat32_leaf_name_from_path(const char* path) {
    const char* leaf = path;
    if (path == 0) {
        return 0;
    }
    for (const char* cursor = path; *cursor != '\0'; cursor++) {
        if (*cursor == '/') {
            leaf = cursor + 1;
        }
    }
    return leaf;
}

int FAT32Driver::find_free_dir_slots(uint32_t dir_cluster,
                                     uint32_t slot_count,
                                     uint32_t* out_dir_cluster,
                                     uint32_t* out_sector_offset,
                                     uint32_t* out_entry_index) {
    if (!initialized) {
        return VFS_ERR_NOT_READY;
    }
    if (slot_count == 0 || slot_count > (bpb.bytes_per_sector / sizeof(FAT32DirEntry))) {
        return VFS_ERR_INVALID_PATH;
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
            for (uint32_t i = 0; i + slot_count <= entry_count; i++) {
                uint8_t free_run = 1;
                for (uint32_t j = 0; j < slot_count; j++) {
                    if (dir[i + j].name[0] != 0x00 && dir[i + j].name[0] != 0xE5) {
                        free_run = 0;
                        break;
                    }
                }
                if (free_run) {
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

int FAT32Driver::find_free_dir_slot(uint32_t dir_cluster,
                                    uint32_t* out_dir_cluster,
                                    uint32_t* out_sector_offset,
                                    uint32_t* out_entry_index) {
    return find_free_dir_slots(dir_cluster, 1, out_dir_cluster, out_sector_offset, out_entry_index);
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

bool FAT32Driver::read_directory_parent_link(uint32_t dir_cluster, uint32_t* parent_cluster_out) {
    uint8_t sector[512];
    FAT32DirEntry* dir = (FAT32DirEntry*)sector;
    if (parent_cluster_out == 0) {
        return false;
    }
    if (!read_dir_sector(dir_cluster, 0, sector)) {
        return false;
    }
    *parent_cluster_out = dir_entry_cluster(dir[1]);
    return true;
}

bool FAT32Driver::update_directory_parent_link(uint32_t dir_cluster, uint32_t parent_cluster) {
    uint8_t sector[512];
    FAT32DirEntry* dir = (FAT32DirEntry*)sector;
    if (!read_dir_sector(dir_cluster, 0, sector)) {
        return false;
    }
    dir[1].first_cluster_high = (uint16_t)((parent_cluster >> 16) & 0xFFFFu);
    dir[1].first_cluster_low = (uint16_t)(parent_cluster & 0xFFFFu);
    return write_dir_sector(dir_cluster, 0, sector);
}

bool FAT32Driver::directory_is_descendant_of(uint32_t dir_cluster, uint32_t possible_ancestor) {
    uint32_t current = dir_cluster;
    uint32_t guard = 0;
    while (current >= 2u && guard < 256u) {
        if (current == possible_ancestor) {
            return true;
        }
        if (current == bpb.root_cluster) {
            break;
        }

        uint32_t parent = 0;
        if (!read_directory_parent_link(current, &parent)) {
            return false;
        }
        if (parent == current || parent < 2u) {
            break;
        }
        current = parent;
        guard++;
    }
    return false;
}

int FAT32Driver::delete_entry_chain(uint32_t dir_cluster, uint32_t sector_offset, uint32_t entry_index, const FAT32DirEntry* entry) {
    uint8_t sector[512];
    if (entry == 0) {
        return VFS_ERR_INVALID_PATH;
    }
    if (!read_dir_sector(dir_cluster, sector_offset, sector)) {
        return VFS_ERR_IO;
    }

    FAT32DirEntry* dir = (FAT32DirEntry*)sector;
    dir[entry_index].name[0] = 0xE5;

    uint8_t checksum = fat32_short_name_checksum(entry->name);
    for (int32_t i = (int32_t)entry_index - 1; i >= 0; i--) {
        FAT32DirEntry* current = &dir[i];
        if (current->attributes != 0x0F) {
            break;
        }
        FAT32LFNEntry lfn = {};
        for (uint32_t j = 0; j < sizeof(FAT32LFNEntry); j++) {
            ((uint8_t*)&lfn)[j] = ((const uint8_t*)current)[j];
        }
        if (lfn.checksum != checksum) {
            break;
        }
        current->name[0] = 0xE5;
    }

    return write_dir_sector(dir_cluster, sector_offset, sector) ? VFS_OK : VFS_ERR_IO;
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

    const char* leaf = fat32_leaf_name_from_path(path);
    uint8_t needs_lfn = !fat32_is_simple_83_name(leaf != 0 ? leaf : "");
    FAT32DirEntry existing = {};
    uint32_t existing_dir_cluster = 0;
    uint32_t existing_sector_offset = 0;
    uint32_t existing_entry_index = 0;
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
    uint32_t lfn_count = needs_lfn ? fat32_lfn_slot_count(leaf) : 0;

    if (found == VFS_OK) {
        if ((existing.attributes & 0x10) != 0) {
            return VFS_ERR_INVALID_PATH;
        }
        target_dir_cluster = existing_dir_cluster;
        target_sector_offset = existing_sector_offset;
        target_entry_index = existing_entry_index;
        old_cluster = dir_entry_cluster(existing);
        for (uint32_t i = 0; i < 11; i++) {
            leaf83[i] = (char)existing.name[i];
        }
    } else if (found == VFS_ERR_NOT_FOUND) {
        if (needs_lfn) {
            for (uint32_t serial = 1; serial <= 99; serial++) {
                FAT32DirEntry alias_entry;
                fat32_build_short_alias_name(leaf, serial, leaf83);
                if (find_short_name_in_directory(parent_cluster, leaf83, &alias_entry) == VFS_ERR_NOT_FOUND) {
                    break;
                }
                if (serial == 99) {
                    return VFS_ERR_IO;
                }
            }
        }
        result = find_free_dir_slots(parent_cluster,
                                     needs_lfn ? (lfn_count + 1u) : 1u,
                                     &target_dir_cluster,
                                     &target_sector_offset,
                                     &target_entry_index);
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

    if (!needs_lfn || found == VFS_OK) {
        if (!update_dir_entry(target_dir_cluster, target_sector_offset, target_entry_index, &new_entry)) {
            if (first_cluster >= 2u) {
                free_cluster_chain(first_cluster);
            }
            return VFS_ERR_IO;
        }
    } else {
        FAT32DirEntry entries[8];
        if (lfn_count + 1u > 8u) {
            if (first_cluster >= 2u) {
                free_cluster_chain(first_cluster);
            }
            return VFS_ERR_IO;
        }
        uint32_t leaf_len = 0;
        while (leaf[leaf_len] != '\0') {
            leaf_len++;
        }
        for (uint32_t i = 0; i < lfn_count; i++) {
            FAT32LFNEntry lfn_entry = {};
            fat32_make_lfn_entry(leaf,
                                 leaf_len,
                                 lfn_count - 1u - i,
                                 lfn_count,
                                 fat32_short_name_checksum((const uint8_t*)leaf83),
                                 &lfn_entry);
            for (uint32_t j = 0; j < sizeof(FAT32LFNEntry); j++) {
                ((uint8_t*)&entries[i])[j] = ((const uint8_t*)&lfn_entry)[j];
            }
        }
        fat32_copy_dir_entry(&entries[lfn_count], &new_entry);

        uint8_t sector[512];
        if (!read_dir_sector(target_dir_cluster, target_sector_offset, sector)) {
            if (first_cluster >= 2u) {
                free_cluster_chain(first_cluster);
            }
            return VFS_ERR_IO;
        }
        FAT32DirEntry* dir = (FAT32DirEntry*)sector;
        for (uint32_t i = 0; i < lfn_count + 1u; i++) {
            fat32_copy_dir_entry(&dir[target_entry_index + i], &entries[i]);
        }
        if (!write_dir_sector(target_dir_cluster, target_sector_offset, sector)) {
            if (first_cluster >= 2u) {
                free_cluster_chain(first_cluster);
            }
            return VFS_ERR_IO;
        }
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
    const char* leaf = fat32_leaf_name_from_path(path);
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

    result = delete_entry_chain(dir_cluster, sector_offset, entry_index, &existing);
    if (result != VFS_OK) {
        return result;
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

    const char* leaf = fat32_leaf_name_from_path(path);
    uint8_t needs_lfn = !fat32_is_simple_83_name(leaf != 0 ? leaf : "");
    FAT32DirEntry existing = {};
    result = find_in_directory(parent_cluster, leaf != 0 ? leaf : "", &existing);
    if (result == VFS_OK) {
        return VFS_ERR_ALREADY_MOUNTED;
    }
    if (result != VFS_ERR_NOT_FOUND) {
        return result;
    }

    uint32_t lfn_count = needs_lfn ? fat32_lfn_slot_count(leaf) : 0;
    if (needs_lfn) {
        for (uint32_t serial = 1; serial <= 99; serial++) {
            FAT32DirEntry alias_entry;
            fat32_build_short_alias_name(leaf, serial, leaf83);
            if (find_short_name_in_directory(parent_cluster, leaf83, &alias_entry) == VFS_ERR_NOT_FOUND) {
                break;
            }
            if (serial == 99) {
                return VFS_ERR_IO;
            }
        }
    }

    uint32_t target_dir_cluster = 0;
    uint32_t target_sector_offset = 0;
    uint32_t target_entry_index = 0;
    result = find_free_dir_slots(parent_cluster,
                                 needs_lfn ? (lfn_count + 1u) : 1u,
                                 &target_dir_cluster,
                                 &target_sector_offset,
                                 &target_entry_index);
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

    if (!needs_lfn) {
        if (!update_dir_entry(target_dir_cluster, target_sector_offset, target_entry_index, &new_entry)) {
            free_cluster_chain(new_cluster);
            return VFS_ERR_IO;
        }
    } else {
        FAT32DirEntry entries[8];
        if (lfn_count + 1u > 8u) {
            free_cluster_chain(new_cluster);
            return VFS_ERR_IO;
        }
        uint32_t leaf_len = 0;
        while (leaf[leaf_len] != '\0') {
            leaf_len++;
        }
        for (uint32_t i = 0; i < lfn_count; i++) {
            FAT32LFNEntry lfn_entry = {};
            fat32_make_lfn_entry(leaf,
                                 leaf_len,
                                 lfn_count - 1u - i,
                                 lfn_count,
                                 fat32_short_name_checksum((const uint8_t*)leaf83),
                                 &lfn_entry);
            for (uint32_t j = 0; j < sizeof(FAT32LFNEntry); j++) {
                ((uint8_t*)&entries[i])[j] = ((const uint8_t*)&lfn_entry)[j];
            }
        }
        fat32_copy_dir_entry(&entries[lfn_count], &new_entry);

        if (!read_dir_sector(target_dir_cluster, target_sector_offset, sector)) {
            free_cluster_chain(new_cluster);
            return VFS_ERR_IO;
        }
        FAT32DirEntry* dir = (FAT32DirEntry*)sector;
        for (uint32_t i = 0; i < lfn_count + 1u; i++) {
            fat32_copy_dir_entry(&dir[target_entry_index + i], &entries[i]);
        }
        if (!write_dir_sector(target_dir_cluster, target_sector_offset, sector)) {
            free_cluster_chain(new_cluster);
            return VFS_ERR_IO;
        }
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

    const char* leaf = fat32_leaf_name_from_path(path);

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

    result = delete_entry_chain(dir_cluster, sector_offset, entry_index, &existing);
    if (result != VFS_OK) {
        return result;
    }

    if (!free_cluster_chain(target_cluster)) {
        return VFS_ERR_IO;
    }
    return VFS_OK;
}

int FAT32Driver::rename_path(const char* old_path, const char* new_path) {
    uint32_t old_parent_cluster = 0;
    uint32_t new_parent_cluster = 0;
    char unused_old_leaf83[11];
    char new_leaf83[11];
    const char* old_leaf = fat32_leaf_name_from_path(old_path);
    const char* new_leaf = fat32_leaf_name_from_path(new_path);
    FAT32DirEntry source_entry = {};
    FAT32DirEntry clash_entry = {};
    uint32_t source_dir_cluster = 0;
    uint32_t source_sector_offset = 0;
    uint32_t source_entry_index = 0;
    uint32_t dest_dir_cluster = 0;
    uint32_t dest_sector_offset = 0;
    uint32_t dest_entry_index = 0;
    uint32_t source_cluster = 0;
    uint32_t lfn_count = 0;
    uint8_t needs_lfn = 0;

    if (old_path == 0 || new_path == 0 || old_path[0] == '\0' || new_path[0] == '\0') {
        return VFS_ERR_INVALID_PATH;
    }

    int result = resolve_parent_path(old_path, &old_parent_cluster, unused_old_leaf83);
    if (result != VFS_OK) {
        return result;
    }
    result = resolve_parent_path(new_path, &new_parent_cluster, new_leaf83);
    if (result != VFS_OK) {
        return result;
    }

    if (fat32_name_eq_ci(old_path, new_path)) {
        return VFS_OK;
    }

    result = find_in_directory_with_location(old_parent_cluster,
                                             old_leaf != 0 ? old_leaf : "",
                                             &source_entry,
                                             &source_dir_cluster,
                                             &source_sector_offset,
                                             &source_entry_index);
    if (result != VFS_OK) {
        return result;
    }

    result = find_in_directory(new_parent_cluster, new_leaf != 0 ? new_leaf : "", &clash_entry);
    if (result == VFS_OK) {
        return VFS_ERR_ALREADY_MOUNTED;
    }
    if (result != VFS_ERR_NOT_FOUND) {
        return result;
    }

    source_cluster = dir_entry_cluster(source_entry);
    if ((source_entry.attributes & 0x10) != 0) {
        if (source_cluster < 2u) {
            return VFS_ERR_INVALID_PATH;
        }
        if (new_parent_cluster == source_cluster || directory_is_descendant_of(new_parent_cluster, source_cluster)) {
            return VFS_ERR_INVALID_PATH;
        }
    }

    needs_lfn = !fat32_is_simple_83_name(new_leaf != 0 ? new_leaf : "");
    if (needs_lfn) {
        for (uint32_t serial = 1; serial <= 99; serial++) {
            FAT32DirEntry alias_entry;
            fat32_build_short_alias_name(new_leaf, serial, new_leaf83);
            if (find_short_name_in_directory(new_parent_cluster, new_leaf83, &alias_entry) == VFS_ERR_NOT_FOUND) {
                break;
            }
            if (serial == 99) {
                return VFS_ERR_IO;
            }
        }
        lfn_count = fat32_lfn_slot_count(new_leaf);
    }

    result = find_free_dir_slots(new_parent_cluster,
                                 needs_lfn ? (lfn_count + 1u) : 1u,
                                 &dest_dir_cluster,
                                 &dest_sector_offset,
                                 &dest_entry_index);
    if (result != VFS_OK) {
        return result;
    }

    FAT32DirEntry renamed_entry = {};
    fat32_copy_dir_entry(&renamed_entry, &source_entry);
    for (uint32_t i = 0; i < 11; i++) {
        renamed_entry.name[i] = (uint8_t)new_leaf83[i];
    }

    if (!needs_lfn) {
        if (!update_dir_entry(dest_dir_cluster, dest_sector_offset, dest_entry_index, &renamed_entry)) {
            return VFS_ERR_IO;
        }
    } else {
        FAT32DirEntry entries[8];
        uint8_t sector[512];
        uint32_t new_leaf_len = 0;

        if (lfn_count + 1u > 8u) {
            return VFS_ERR_IO;
        }

        while (new_leaf[new_leaf_len] != '\0') {
            new_leaf_len++;
        }
        for (uint32_t i = 0; i < lfn_count; i++) {
            FAT32LFNEntry lfn_entry = {};
            fat32_make_lfn_entry(new_leaf,
                                 new_leaf_len,
                                 lfn_count - 1u - i,
                                 lfn_count,
                                 fat32_short_name_checksum((const uint8_t*)new_leaf83),
                                 &lfn_entry);
            for (uint32_t j = 0; j < sizeof(FAT32LFNEntry); j++) {
                ((uint8_t*)&entries[i])[j] = ((const uint8_t*)&lfn_entry)[j];
            }
        }
        fat32_copy_dir_entry(&entries[lfn_count], &renamed_entry);

        if (!read_dir_sector(dest_dir_cluster, dest_sector_offset, sector)) {
            return VFS_ERR_IO;
        }
        FAT32DirEntry* dir = (FAT32DirEntry*)sector;
        for (uint32_t i = 0; i < lfn_count + 1u; i++) {
            fat32_copy_dir_entry(&dir[dest_entry_index + i], &entries[i]);
        }
        if (!write_dir_sector(dest_dir_cluster, dest_sector_offset, sector)) {
            return VFS_ERR_IO;
        }
    }

    if ((source_entry.attributes & 0x10) != 0 && old_parent_cluster != new_parent_cluster) {
        if (!update_directory_parent_link(source_cluster, new_parent_cluster)) {
            delete_entry_chain(dest_dir_cluster, dest_sector_offset, dest_entry_index, &renamed_entry);
            return VFS_ERR_IO;
        }
    }

    result = delete_entry_chain(source_dir_cluster, source_sector_offset, source_entry_index, &source_entry);
    if (result != VFS_OK) {
        return result;
    }

    return VFS_OK;
}
