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

