#ifndef FAT12_H
#define FAT12_H

#include "drivers/driver.h"
#include "drivers/ata.h"
#include <stdint.h>

struct BPB {
    uint8_t  jump[3];
    uint8_t  oem[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  fat_count;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t sectors_per_fat;
    uint16_t sectors_per_track;
    uint16_t head_count;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
} __attribute__((packed));

struct DirEntry {
    uint8_t  name[11];      // 8.3 형식
    uint8_t  attributes;
    uint8_t  reserved[10];
    uint16_t time;
    uint16_t date;
    uint16_t first_cluster;
    uint32_t file_size;
} __attribute__((packed));

class FAT12Driver : public Driver {
    ATADriver* ata;
    BPB bpb;

    uint32_t fat_start;
    uint32_t root_start;
    uint32_t data_start;

    uint8_t fat_table[512 * 9];  // FAT 테이블 캐시

    uint16_t get_fat_entry(uint16_t cluster);
    uint32_t cluster_to_sector(uint16_t cluster);

    uint16_t find_free_cluster();
    void set_fat_entry(uint16_t cluster, uint16_t value);
    void flush_fat();
    bool add_root_entry(const char* name83, uint16_t cluster, uint32_t size);

public:
    FAT12Driver(ATADriver* ata);
    void init() override;
    bool find_file(const char* name, DirEntry* entry);
    int read_file(DirEntry* entry, uint8_t* buffer);
    void list_files();
    bool write_file(const char* name83, uint8_t* buffer, uint32_t size);
    bool delete_file(const char* name83);
};

#endif