#ifndef FAT32_H
#define FAT32_H

#include "drivers/driver.h"
#include "drivers/ata.h"
#include "fs/vfs.h"

#include <stdint.h>

struct FAT32BPB {
    uint8_t  jump[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t sectors_per_fat_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];
} __attribute__((packed));

struct FAT32DirEntry {
    uint8_t  name[11];
    uint8_t  attributes;
    uint8_t  nt_reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t first_cluster_high;
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed));

class FAT32Driver : public Driver {
    ATADriver* ata;
    uint32_t start_lba;
    FAT32BPB bpb;
    uint8_t initialized;

    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t total_sectors;

    bool read_sector_relative(uint32_t lba, uint8_t* buffer);
    uint32_t cluster_to_lba(uint32_t cluster) const;
    uint32_t read_fat_entry(uint32_t cluster);
    bool read_cluster_sector(uint32_t cluster, uint32_t sector_offset, uint8_t* buffer);
    bool is_end_of_chain(uint32_t value) const;
    void to_name83(const char* path_segment, char out[11]) const;
    bool dir_entry_name(const FAT32DirEntry& entry, char* out, uint32_t capacity) const;
    uint32_t dir_entry_cluster(const FAT32DirEntry& entry) const;

    int read_dir_entry_internal(uint32_t dir_cluster, uint32_t visible_index, FAT32DirEntry* entry);
    int find_in_directory(uint32_t dir_cluster, const char* segment, FAT32DirEntry* entry);
    int resolve_path(const char* path, FAT32DirEntry* entry, uint32_t* out_cluster, uint8_t* out_is_root_dir);

public:
    FAT32Driver(ATADriver* ata, uint32_t start_lba = 0);
    void init() override;
    bool ready() const;

    int list_dir(const char* path);
    int read_dir_entry(const char* path, uint32_t index, VFSDirEntry* entry);
    int get_path_info(const char* path, VFSFileInfo* info);
    int read_file_path(const char* path, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read_out);
};

#endif
