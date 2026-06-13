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

        if (fat32_str_eq(current_name, segment)) {
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

                if (!fat32_str_eq(current_name, segment)) {
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
