#include "fat12.h"
#include "drivers/terminal.hpp"

extern "C" {
    #include "io.h"
    #include "heap.h"
}

extern Terminal terminal;  // 포인터 아닌 참조로

FAT12Driver::FAT12Driver(ATADriver* ata) : ata(ata) {}

void FAT12Driver::init() {
    terminal.print("1\n");
    uint8_t buffer[512];
    ata->read_sector(0, buffer);

    terminal.print("2\n");
    for (int i = 0; i < (int)sizeof(BPB); i++) {
        ((uint8_t*)&bpb)[i] = buffer[i];
    }

    terminal.print("3\n");
    fat_start  = bpb.reserved_sectors;
    root_start = fat_start + (bpb.fat_count * bpb.sectors_per_fat);
    data_start = root_start + ((bpb.root_entry_count * 32) / bpb.bytes_per_sector);

    terminal.print("4\n");
    terminal.print("sectors_per_fat: ");
    terminal.print_hex(bpb.sectors_per_fat);
    terminal.print("\n");
    //fat_table = (uint8_t*)kmalloc(512 * bpb.sectors_per_fat);
    terminal.print("fat_table: ");
    terminal.print_hex((uint32_t)fat_table);
    terminal.print("\n");

    terminal.print("5\n");
    for (int i = 0; i < bpb.sectors_per_fat; i++) {
        ata->read_sector(fat_start + i, fat_table + i * 512);
    }
    terminal.print("6\n");
}

uint16_t FAT12Driver::get_fat_entry(uint16_t cluster) {
    uint32_t offset = cluster + (cluster / 2);
    uint16_t val = *(uint16_t*)(fat_table + offset);
    if (cluster & 1)
        return val >> 4;
    else
        return val & 0x0FFF;
}

uint32_t FAT12Driver::cluster_to_sector(uint16_t cluster) {
    return data_start + (cluster - 2) * bpb.sectors_per_cluster;
}

bool FAT12Driver::find_file(const char* name, DirEntry* entry) {
    uint8_t buffer[512];
    int entries_per_sector = 512 / 32;
    int root_sectors = (bpb.root_entry_count * 32) / 512;

    for (int s = 0; s < root_sectors; s++) {
        ata->read_sector(root_start + s, buffer);
        DirEntry* dir = (DirEntry*)buffer;

        for (int i = 0; i < entries_per_sector; i++) {
            if (dir[i].name[0] == 0x00) return false;  // 끝
            if (dir[i].name[0] == 0xE5) continue;       // 삭제된 파일

            // 이름 비교 (8.3 형식)
            bool match = true;
            for (int j = 0; j < 11; j++) {
                if (dir[i].name[j] != name[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                *entry = dir[i];
                return true;
            }
        }
    }
    return false;
}

int FAT12Driver::read_file(DirEntry* entry, uint8_t* buffer) {
    uint16_t cluster = entry->first_cluster;
    int offset = 0;
    int limit = 0;

    while (cluster >= 0x002 && cluster < 0xFF8 && limit < 100) {
        uint32_t sector = cluster_to_sector(cluster);
        ata->read_sector(sector, buffer + offset);
        offset += 512;
        cluster = get_fat_entry(cluster);
        limit++;
    }
    return offset;
}

void FAT12Driver::list_files() {
    uint8_t buffer[512];
    int entries_per_sector = 512 / 32;
    int root_sectors = (bpb.root_entry_count * 32) / 512;

    for (int s = 0; s < root_sectors; s++) {
        ata->read_sector(root_start + s, buffer);
        DirEntry* dir = (DirEntry*)buffer;

        for (int i = 0; i < entries_per_sector; i++) {
            if (dir[i].name[0] == 0x00) return;
            if (dir[i].name[0] == 0xE5) continue;
            if (dir[i].attributes & 0x08) continue;  // 볼륨 레이블 스킵

            // 8.3 이름 출력
            char name[13];
            int n = 0;
            for (int j = 0; j < 8 && dir[i].name[j] != ' '; j++)
                name[n++] = dir[i].name[j];
            name[n++] = '.';
            for (int j = 8; j < 11 && dir[i].name[j] != ' '; j++)
                name[n++] = dir[i].name[j];
            name[n] = '\0';

            terminal.print("\n");
            terminal.print(name);
        }
    }
}



uint16_t FAT12Driver::find_free_cluster() {
    for (uint16_t i = 2; i < 2848; i++) {
        if (get_fat_entry(i) == 0x000) return i;
    }
    return 0;  // 디스크 꽉 참
}

void FAT12Driver::set_fat_entry(uint16_t cluster, uint16_t value) {
    uint32_t offset = cluster + (cluster / 2);
    if (cluster & 1) {
        fat_table[offset]     = (fat_table[offset] & 0x0F) | ((value & 0x0F) << 4);
        fat_table[offset + 1] = (value >> 4) & 0xFF;
    } else {
        fat_table[offset]     = value & 0xFF;
        fat_table[offset + 1] = (fat_table[offset + 1] & 0xF0) | ((value >> 8) & 0x0F);
    }
}

void FAT12Driver::flush_fat() {
    for (int i = 0; i < bpb.sectors_per_fat; i++) {
        ata->write_sector(fat_start + i, fat_table + i * 512);
        // FAT2도 업데이트
        ata->write_sector(fat_start + bpb.sectors_per_fat + i, fat_table + i * 512);
    }
}

bool FAT12Driver::add_root_entry(const char* name83, uint16_t cluster, uint32_t size) {
    uint8_t buffer[512];
    int entries_per_sector = 512 / 32;
    int root_sectors = (bpb.root_entry_count * 32) / 512;

    for (int s = 0; s < root_sectors; s++) {
        ata->read_sector(root_start + s, buffer);
        DirEntry* dir = (DirEntry*)buffer;

        for (int i = 0; i < entries_per_sector; i++) {
            if (dir[i].name[0] == 0x00 || dir[i].name[0] == 0xE5) {
                // 빈 슬롯 발견
                for (int j = 0; j < 11; j++)
                    dir[i].name[j] = name83[j];
                dir[i].attributes    = 0x20;  // 일반 파일
                dir[i].first_cluster = cluster;
                dir[i].file_size     = size;
                dir[i].time          = 0;
                dir[i].date          = 0;
                for (int j = 0; j < 10; j++)
                    dir[i].reserved[j] = 0;

                ata->write_sector(root_start + s, buffer);
                return true;
            }
        }
    }
    return false;  // 루트 디렉토리 꽉 참
}

/*bool FAT12Driver::write_file(const char* name83, uint8_t* buffer, uint32_t size) {
    uint32_t bytes_left = size;
    uint16_t first_cluster = 0;
    uint16_t prev_cluster = 0;

    while (bytes_left > 0) {
        uint16_t cluster = find_free_cluster();
        if (cluster == 0) return false;  // 디스크 꽉 참

        // 첫 클러스터 기록
        if (first_cluster == 0) first_cluster = cluster;

        // 이전 클러스터에서 현재 클러스터로 연결
        if (prev_cluster != 0)
            set_fat_entry(prev_cluster, cluster);

        // 마지막 클러스터 표시 (일단)
        set_fat_entry(cluster, 0xFFF);

        // 데이터 쓰기
        uint32_t offset = size - bytes_left;
        uint8_t sector_buf[512];
        for (int i = 0; i < 512; i++)
            sector_buf[i] = (i < (int)bytes_left) ? buffer[offset + i] : 0;

        ata->write_sector(cluster_to_sector(cluster), sector_buf);

        prev_cluster = cluster;
        bytes_left = (bytes_left > 512) ? bytes_left - 512 : 0;
    }

    flush_fat();
    add_root_entry(name83, first_cluster, size);
    return true;
}*/

bool FAT12Driver::write_file(const char* name83, uint8_t* buffer, uint32_t size) {
    // 중복 체크 — 기존 파일 있으면 클러스터 체인 해제
    DirEntry existing;
    if (find_file(name83, &existing)) {
        // 기존 클러스터 체인 전부 해제
        uint16_t cluster = existing.first_cluster;
        while (cluster >= 0x002 && cluster < 0xFF8) {
            uint16_t next = get_fat_entry(cluster);
            set_fat_entry(cluster, 0x000);  // 클러스터 비우기
            cluster = next;
        }
        flush_fat();

        // 루트 엔트리에서 기존 파일 삭제 (0xE5로 표시)
        uint8_t buf[512];
        int entries_per_sector = 512 / 32;
        int root_sectors = (bpb.root_entry_count * 32) / 512;

        for (int s = 0; s < root_sectors; s++) {
            ata->read_sector(root_start + s, buf);
            DirEntry* dir = (DirEntry*)buf;
            for (int i = 0; i < entries_per_sector; i++) {
                bool match = true;
                for (int j = 0; j < 11; j++) {
                    if (dir[i].name[j] != name83[j]) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    dir[i].name[0] = 0xE5;  // 삭제 표시
                    ata->write_sector(root_start + s, buf);
                    goto done;
                }
            }
        }
        done:;
    }

    // 기존 write_file 로직 그대로
    uint32_t bytes_left = size;
    uint16_t first_cluster = 0;
    uint16_t prev_cluster = 0;

    while (bytes_left > 0) {
        uint16_t cluster = find_free_cluster();
        if (cluster == 0) return false;

        if (first_cluster == 0) first_cluster = cluster;
        if (prev_cluster != 0)
            set_fat_entry(prev_cluster, cluster);
        set_fat_entry(cluster, 0xFFF);

        uint32_t offset = size - bytes_left;
        uint8_t sector_buf[512];
        for (int i = 0; i < 512; i++)
            sector_buf[i] = (i < (int)bytes_left) ? buffer[offset + i] : 0;

        ata->write_sector(cluster_to_sector(cluster), sector_buf);

        prev_cluster = cluster;
        bytes_left = (bytes_left > 512) ? bytes_left - 512 : 0;
    }

    flush_fat();
    add_root_entry(name83, first_cluster, size);
    return true;
}

bool FAT12Driver::delete_file(const char* name83) {
    uint8_t buffer[512];
    int entries_per_sector = 512 / 32;
    int root_sectors = (bpb.root_entry_count * 32) / 512;

    for (int s = 0; s < root_sectors; s++) {
        ata->read_sector(root_start + s, buffer);
        DirEntry* dir = (DirEntry*)buffer;

        for (int i = 0; i < entries_per_sector; i++) {
            if (dir[i].name[0] == 0x00) return false;  // 끝
            if (dir[i].name[0] == 0xE5) continue;       // 이미 삭제됨

            // 이름 비교
            bool match = true;
            for (int j = 0; j < 11; j++) {
                if (dir[i].name[j] != name83[j]) {
                    match = false;
                    break;
                }
            }

            if (match) {
                // FAT 체인 해제
                uint16_t cluster = dir[i].first_cluster;
                while (cluster >= 0x002 && cluster < 0xFF8) {
                    uint16_t next = get_fat_entry(cluster);
                    set_fat_entry(cluster, 0x000);  // 빈 클러스터로
                    cluster = next;
                }
                flush_fat();

                // 디렉토리 엔트리 삭제 표시
                dir[i].name[0] = 0xE5;
                ata->write_sector(root_start + s, buffer);
                return true;
            }
        }
    }
    return false;
}