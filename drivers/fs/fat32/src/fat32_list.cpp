int FAT32Driver::list_dir(const char* path) {
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

    uint8_t sector[512];
    uint32_t cluster = dir_cluster;
    char lfn_name[64];
    char entry_name[64];
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
                    return VFS_OK;
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

                if (lfn_active && lfn_name[0] != '\0' &&
                    fat32_short_name_checksum(dir[i].name) == lfn_checksum) {
                    uint32_t j = 0;
                    while (lfn_name[j] != '\0' && j + 1 < sizeof(entry_name)) {
                        entry_name[j] = lfn_name[j];
                        j++;
                    }
                    entry_name[j] = '\0';
                } else if (!dir_entry_name(dir[i], entry_name, sizeof(entry_name))) {
                    return VFS_ERR_IO;
                }

                fat32_print("\n");
                fat32_print(entry_name);
                if (dir[i].attributes & 0x10) {
                    fat32_print("/");
                }

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

    return VFS_OK;
}
