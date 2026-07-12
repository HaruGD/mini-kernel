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

struct FAT32LFNEntry {
    uint8_t  order;
    uint16_t name1[5];
    uint8_t  attributes;
    uint8_t  entry_type;
    uint8_t  checksum;
    uint16_t name2[6];
    uint16_t first_cluster_low;
    uint16_t name3[2];
} __attribute__((packed));

class FAT32Driver : public Driver {
    ATADriver* ata;
    uint8_t* ramdisk;
    uint32_t ramdisk_size;
    uint32_t start_lba;
    FAT32BPB bpb;
    uint8_t initialized;

    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t total_sectors;

    bool read_sector_relative(uint32_t lba, uint8_t* buffer);
    bool write_sector_relative(uint32_t lba, const uint8_t* buffer);
    uint32_t cluster_to_lba(uint32_t cluster) const;
    uint32_t read_fat_entry(uint32_t cluster);
    bool write_fat_entry(uint32_t cluster, uint32_t value);
    bool read_cluster_sector(uint32_t cluster, uint32_t sector_offset, uint8_t* buffer);
    bool write_cluster_sector(uint32_t cluster, uint32_t sector_offset, const uint8_t* buffer);
    bool is_end_of_chain(uint32_t value) const;
    void to_name83(const char* path_segment, char out[11]) const;
    bool dir_entry_name(const FAT32DirEntry& entry, char* out, uint32_t capacity) const;
    uint32_t dir_entry_cluster(const FAT32DirEntry& entry) const;

    int read_dir_entry_internal(uint32_t dir_cluster, uint32_t visible_index, FAT32DirEntry* entry, char* name_out, uint32_t name_capacity);
    int find_in_directory(uint32_t dir_cluster, const char* segment, FAT32DirEntry* entry);
    int find_short_name_in_directory(uint32_t dir_cluster, const char short_name[11], FAT32DirEntry* entry);
    int find_in_directory_with_location(uint32_t dir_cluster,
                                        const char* segment,
                                        FAT32DirEntry* entry,
                                        uint32_t* out_dir_cluster,
                                        uint32_t* out_sector_offset,
                                        uint32_t* out_entry_index);
    int resolve_path(const char* path, FAT32DirEntry* entry, uint32_t* out_cluster, uint8_t* out_is_root_dir);
    int resolve_parent_path(const char* path, uint32_t* out_dir_cluster, char out_leaf83[11]);
    int find_free_dir_slots(uint32_t dir_cluster,
                            uint32_t slot_count,
                            uint32_t* out_dir_cluster,
                            uint32_t* out_sector_offset,
                            uint32_t* out_entry_index);
    int find_free_dir_slot(uint32_t dir_cluster, uint32_t* out_dir_cluster, uint32_t* out_sector_offset, uint32_t* out_entry_index);
    bool read_dir_sector(uint32_t dir_cluster, uint32_t sector_offset, uint8_t* sector);
    bool write_dir_sector(uint32_t dir_cluster, uint32_t sector_offset, const uint8_t* sector);
    bool alloc_free_cluster(uint32_t* out_cluster);
    bool free_cluster_chain(uint32_t start_cluster);
    bool write_cluster_chain(const uint8_t* buffer, uint32_t size, uint32_t* out_first_cluster);
    bool update_dir_entry(uint32_t dir_cluster, uint32_t sector_offset, uint32_t entry_index, const FAT32DirEntry* entry);
    bool update_directory_parent_link(uint32_t dir_cluster, uint32_t parent_cluster);
    bool read_directory_parent_link(uint32_t dir_cluster, uint32_t* parent_cluster_out);
    bool directory_is_descendant_of(uint32_t dir_cluster, uint32_t possible_ancestor);
    int delete_entry_chain(uint32_t dir_cluster, uint32_t sector_offset, uint32_t entry_index, const FAT32DirEntry* entry);

public:
    FAT32Driver(ATADriver* ata, uint32_t start_lba = 0);
    FAT32Driver(uint8_t* ramdisk, uint32_t ramdisk_size, uint32_t start_lba = 0);
    void attach_ramdisk(uint8_t* ramdisk, uint32_t ramdisk_size, uint32_t start_lba = 0);
    void init() override;
    bool ready() const;

    int list_dir(const char* path);
    int read_dir_entry(const char* path, uint32_t index, VFSDirEntry* entry);
    int get_path_info(const char* path, VFSFileInfo* info);
    int read_file_path(const char* path, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read_out);
    int write_file_path(const char* path, const uint8_t* buffer, uint32_t size);
    int touch_file_path(const char* path);
    int delete_file_path(const char* path);
    int mkdir_path(const char* path);
    int rmdir_path(const char* path);
    int rename_path(const char* old_path, const char* new_path);
};

#endif
