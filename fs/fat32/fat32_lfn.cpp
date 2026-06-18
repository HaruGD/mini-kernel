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

