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

    if (fat32_str_eq(old_path, new_path)) {
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
