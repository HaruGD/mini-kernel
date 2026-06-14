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
      ramdisk(0),
      ramdisk_size(0),
      start_lba(start_lba),
      initialized(0),
      fat_start_lba(0),
      data_start_lba(0),
      total_sectors(0) {
}

FAT32Driver::FAT32Driver(uint8_t* ramdisk, uint32_t ramdisk_size, uint32_t start_lba)
    : ata(0),
      ramdisk(ramdisk),
      ramdisk_size(ramdisk_size),
      start_lba(start_lba),
      initialized(0),
      fat_start_lba(0),
      data_start_lba(0),
      total_sectors(0) {
}

void FAT32Driver::attach_ramdisk(uint8_t* new_ramdisk, uint32_t new_ramdisk_size, uint32_t new_start_lba) {
    ata = 0;
    ramdisk = new_ramdisk;
    ramdisk_size = new_ramdisk_size;
    start_lba = new_start_lba;
    initialized = 0;
    fat_start_lba = 0;
    data_start_lba = 0;
    total_sectors = 0;
}

bool FAT32Driver::read_sector_relative(uint32_t lba, uint8_t* buffer) {
    if (buffer == 0) {
        return false;
    }
    if (ramdisk != 0) {
        uint64_t offset = (uint64_t)(start_lba + lba) * 512ULL;
        if (offset > ramdisk_size || ramdisk_size - offset < 512ULL) {
            return false;
        }
        for (uint32_t i = 0; i < 512; i++) {
            buffer[i] = ramdisk[offset + i];
        }
        return true;
    }
    if (ata == 0) {
        return false;
    }
    return ata->read_sector(start_lba + lba, buffer);
}

bool FAT32Driver::write_sector_relative(uint32_t lba, const uint8_t* buffer) {
    if (buffer == 0) {
        return false;
    }
    if (ramdisk != 0) {
        uint64_t offset = (uint64_t)(start_lba + lba) * 512ULL;
        if (offset > ramdisk_size || ramdisk_size - offset < 512ULL) {
            return false;
        }
        for (uint32_t i = 0; i < 512; i++) {
            ramdisk[offset + i] = buffer[i];
        }
        return true;
    }
    if (ata == 0) {
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
