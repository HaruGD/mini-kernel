static MemFSNode* memfs_get_node(int16_t index) {
    if (index < 0 || index >= MEMFS_MAX_NODES) {
        return 0;
    }
    if (!g_memfs_nodes[index].active) {
        return 0;
    }
    return &g_memfs_nodes[index];
}

static int memfs_is_reserved_segment(const char* name) {
    return vfs_str_eq(name, ".") || vfs_str_eq(name, "..");
}

static int memfs_find_child(int16_t dir_index, const char* name) {
    MemFSNode* dir = memfs_get_node(dir_index);
    if (dir == 0 || dir->type != VFS_NODE_DIR) {
        return -1;
    }

    int16_t child_index = dir->first_child;
    while (child_index >= 0) {
        MemFSNode* child = memfs_get_node(child_index);
        if (child != 0 && vfs_str_eq(child->name, name)) {
            return child_index;
        }
        child_index = child != 0 ? child->next_sibling : -1;
    }
    return -1;
}

static int memfs_alloc_node(uint8_t type, int16_t parent, const char* name) {
    if (name == 0 || name[0] == '\0' || vfs_strlen_local(name) >= MEMFS_MAX_NAME || memfs_is_reserved_segment(name)) {
        return -1;
    }

    for (int16_t i = 1; i < MEMFS_MAX_NODES; i++) {
        if (g_memfs_nodes[i].active) {
            continue;
        }

        g_memfs_nodes[i].active = 1;
        g_memfs_nodes[i].type = type;
        g_memfs_nodes[i].parent = parent;
        g_memfs_nodes[i].first_child = -1;
        g_memfs_nodes[i].next_sibling = -1;
        g_memfs_nodes[i].size = 0;
        vfs_copy_string(g_memfs_nodes[i].name, sizeof(g_memfs_nodes[i].name), name);

        MemFSNode* parent_node = memfs_get_node(parent);
        if (parent_node != 0) {
            g_memfs_nodes[i].next_sibling = parent_node->first_child;
            parent_node->first_child = i;
        }

        return i;
    }

    return -1;
}

static void memfs_unlink_child(int16_t parent_index, int16_t child_index) {
    MemFSNode* parent = memfs_get_node(parent_index);
    if (parent == 0) {
        return;
    }

    int16_t current = parent->first_child;
    int16_t previous = -1;
    while (current >= 0) {
        MemFSNode* node = memfs_get_node(current);
        int16_t next = node != 0 ? node->next_sibling : -1;
        if (current == child_index) {
            if (previous < 0) {
                parent->first_child = next;
            } else {
                MemFSNode* previous_node = memfs_get_node(previous);
                if (previous_node != 0) {
                    previous_node->next_sibling = next;
                }
            }
            return;
        }
        previous = current;
        current = next;
    }
}

static void memfs_reset_node(int16_t index) {
    MemFSNode* node = memfs_get_node(index);
    if (node == 0) {
        return;
    }

    if (node->parent >= 0) {
        memfs_unlink_child(node->parent, index);
    }

    node->active = 0;
    node->type = VFS_NODE_NONE;
    node->parent = -1;
    node->first_child = -1;
    node->next_sibling = -1;
    node->name[0] = '\0';
    node->size = 0;
}

static int memfs_next_segment(const char** cursor_inout, char* segment_out) {
    const char* cursor = *cursor_inout;
    uint32_t length = 0;

    if (cursor == 0) {
        return 0;
    }

    while (*cursor == '/') {
        return -1;
    }
    if (*cursor == '\0') {
        return 0;
    }

    while (cursor[length] != '\0' && cursor[length] != '/') {
        if (length + 1 >= MEMFS_MAX_NAME) {
            return -1;
        }
        segment_out[length] = cursor[length];
        length++;
    }
    segment_out[length] = '\0';
    if (length == 0 || memfs_is_reserved_segment(segment_out)) {
        return -1;
    }

    cursor += length;
    if (*cursor == '/') {
        cursor++;
        if (*cursor == '\0') {
            return -1;
        }
    }
    *cursor_inout = cursor;
    return 1;
}

static int memfs_lookup_path(const char* relative_path, int16_t* out_node_index) {
    const char* cursor = relative_path;
    int16_t current = 0;

    if (out_node_index == 0) {
        return VFS_ERR_IO;
    }
    if (relative_path == 0 || relative_path[0] == '\0') {
        *out_node_index = 0;
        return VFS_OK;
    }

    while (1) {
        char segment[MEMFS_MAX_NAME];
        int step = memfs_next_segment(&cursor, segment);
        if (step < 0) {
            return VFS_ERR_INVALID_PATH;
        }
        if (step == 0) {
            *out_node_index = current;
            return VFS_OK;
        }

        int child = memfs_find_child(current, segment);
        if (child < 0) {
            return VFS_ERR_NOT_FOUND;
        }
        current = (int16_t)child;
    }
}

static int memfs_lookup_parent(const char* relative_path, int16_t* out_parent_index, char* out_leaf_name) {
    const char* cursor = relative_path;
    int16_t current = 0;
    char segment[MEMFS_MAX_NAME];

    if (relative_path == 0 || relative_path[0] == '\0' || out_parent_index == 0 || out_leaf_name == 0) {
        return VFS_ERR_INVALID_PATH;
    }

    while (1) {
        const char* next_cursor = cursor;
        int step = memfs_next_segment(&next_cursor, segment);
        if (step <= 0) {
            return VFS_ERR_INVALID_PATH;
        }

        if (*next_cursor == '\0') {
            *out_parent_index = current;
            vfs_copy_string(out_leaf_name, MEMFS_MAX_NAME, segment);
            return VFS_OK;
        }

        int child = memfs_find_child(current, segment);
        if (child < 0) {
            return VFS_ERR_NOT_FOUND;
        }

        MemFSNode* child_node = memfs_get_node((int16_t)child);
        if (child_node == 0 || child_node->type != VFS_NODE_DIR) {
            return VFS_ERR_NOT_FOUND;
        }

        current = (int16_t)child;
        cursor = next_cursor;
    }
}

static int memfs_backend_list_files(void*, const char* relative_path) {
    int16_t dir_index = 0;
    int result = memfs_lookup_path(relative_path != 0 ? relative_path : "", &dir_index);
    if (result != VFS_OK) {
        return result;
    }

    MemFSNode* dir = memfs_get_node(dir_index);
    if (dir == 0 || dir->type != VFS_NODE_DIR) {
        return VFS_ERR_INVALID_PATH;
    }

    int16_t child_index = dir->first_child;
    while (child_index >= 0) {
        MemFSNode* child = memfs_get_node(child_index);
        if (child != 0) {
            vfs_print("\n");
            vfs_print(child->name);
            if (child->type == VFS_NODE_DIR) {
                vfs_print("/");
            }
        }
        child_index = child != 0 ? child->next_sibling : -1;
    }
    return VFS_OK;
}

static int memfs_backend_read_dir_entry(void*, const char* relative_path, uint32_t cursor, VFSDirEntry* entry) {
    int16_t dir_index = 0;
    int result = memfs_lookup_path(relative_path != 0 ? relative_path : "", &dir_index);
    if (result != VFS_OK) {
        return result;
    }

    MemFSNode* dir = memfs_get_node(dir_index);
    if (dir == 0 || dir->type != VFS_NODE_DIR || entry == 0) {
        return VFS_ERR_INVALID_PATH;
    }

    int16_t child_index = dir->first_child;
    uint32_t skip = cursor;
    while (skip > 0 && child_index >= 0) {
        MemFSNode* child = memfs_get_node(child_index);
        child_index = child != 0 ? child->next_sibling : -1;
        skip--;
    }

    if (child_index < 0) {
        return 0;
    }

    MemFSNode* child = memfs_get_node(child_index);
    if (child == 0) {
        return 0;
    }

    entry->type = child->type;
    entry->size = child->type == VFS_NODE_FILE ? child->size : 0;
    vfs_copy_string(entry->name, sizeof(entry->name), child->name);
    return 1;
}

static int memfs_backend_get_file_info(void*, const char* relative_path, VFSFileInfo* info) {
    int16_t node_index = 0;
    int result = memfs_lookup_path(relative_path, &node_index);
    if (result != VFS_OK) {
        return result;
    }

    MemFSNode* node = memfs_get_node(node_index);
    if (node == 0) {
        return VFS_ERR_NOT_FOUND;
    }

    if (info != 0) {
        info->type = node->type;
        info->size = node->type == VFS_NODE_FILE ? node->size : 0;
    }
    return VFS_OK;
}

static int memfs_backend_read_file(void*, const char* relative_path, uint8_t* buffer, uint32_t buffer_size, uint32_t* bytes_read_out) {
    int16_t node_index = 0;
    int result = memfs_lookup_path(relative_path, &node_index);
    if (result != VFS_OK) {
        return result;
    }

    MemFSNode* node = memfs_get_node(node_index);
    if (node == 0 || node->type != VFS_NODE_FILE) {
        return VFS_ERR_INVALID_PATH;
    }
    if (buffer == 0 || buffer_size < (node->size > 0 ? node->size : 1)) {
        return VFS_ERR_BUFFER_TOO_SMALL;
    }

    for (uint32_t i = 0; i < node->size; i++) {
        buffer[i] = node->data[i];
    }
    if (bytes_read_out != 0) {
        *bytes_read_out = node->size;
    }
    return VFS_OK;
}

static int memfs_backend_write_file(void*, const char* relative_path, const uint8_t* buffer, uint32_t size) {
    int16_t parent_index = 0;
    char leaf_name[MEMFS_MAX_NAME];
    if (size > MEMFS_MAX_FILE_SIZE || (size > 0 && buffer == 0)) {
        return VFS_ERR_IO;
    }

    int result = memfs_lookup_parent(relative_path, &parent_index, leaf_name);
    if (result != VFS_OK) {
        return result;
    }

    int child = memfs_find_child(parent_index, leaf_name);
    MemFSNode* node = child >= 0 ? memfs_get_node((int16_t)child) : 0;
    if (node == 0) {
        int allocated = memfs_alloc_node(VFS_NODE_FILE, parent_index, leaf_name);
        if (allocated < 0) {
            return VFS_ERR_IO;
        }
        node = memfs_get_node((int16_t)allocated);
    } else if (node->type != VFS_NODE_FILE) {
        return VFS_ERR_INVALID_PATH;
    }

    for (uint32_t i = 0; i < size; i++) {
        node->data[i] = buffer[i];
    }
    node->size = (uint16_t)size;
    return VFS_OK;
}

static int memfs_backend_touch_file(void* backend_ctx, const char* relative_path) {
    return memfs_backend_write_file(backend_ctx, relative_path, 0, 0);
}

static int memfs_backend_delete_file(void*, const char* relative_path) {
    int16_t node_index = 0;
    int result = memfs_lookup_path(relative_path, &node_index);
    if (result != VFS_OK) {
        return result;
    }

    MemFSNode* node = memfs_get_node(node_index);
    if (node == 0 || node->type != VFS_NODE_FILE) {
        return VFS_ERR_INVALID_PATH;
    }

    memfs_reset_node(node_index);
    return VFS_OK;
}

static int memfs_backend_mkdir(void*, const char* relative_path) {
    int16_t parent_index = 0;
    char leaf_name[MEMFS_MAX_NAME];
    int result = memfs_lookup_parent(relative_path, &parent_index, leaf_name);
    if (result != VFS_OK) {
        return result;
    }
    if (memfs_find_child(parent_index, leaf_name) >= 0) {
        return VFS_ERR_ALREADY_MOUNTED;
    }
    return memfs_alloc_node(VFS_NODE_DIR, parent_index, leaf_name) >= 0 ? VFS_OK : VFS_ERR_IO;
}

static int memfs_backend_rmdir(void*, const char* relative_path) {
    int16_t node_index = 0;
    int result = memfs_lookup_path(relative_path, &node_index);
    if (result != VFS_OK) {
        return result;
    }
    if (node_index == 0) {
        return VFS_ERR_UNSUPPORTED;
    }

    MemFSNode* node = memfs_get_node(node_index);
    if (node == 0 || node->type != VFS_NODE_DIR) {
        return VFS_ERR_INVALID_PATH;
    }
    if (node->first_child >= 0) {
        return VFS_ERR_IO;
    }

    memfs_reset_node(node_index);
    return VFS_OK;
}

static int memfs_is_descendant(int16_t node_index, int16_t possible_ancestor) {
    int16_t current = node_index;
    while (current >= 0) {
        if (current == possible_ancestor) {
            return 1;
        }
        MemFSNode* node = memfs_get_node(current);
        if (node == 0) {
            break;
        }
        current = node->parent;
    }
    return 0;
}

static int memfs_backend_rename_path(void*, const char* old_relative_path, const char* new_relative_path) {
    int16_t node_index = 0;
    int16_t new_parent_index = 0;
    char new_leaf_name[MEMFS_MAX_NAME];
    int result = memfs_lookup_path(old_relative_path, &node_index);
    if (result != VFS_OK) {
        return result;
    }
    if (node_index == 0) {
        return VFS_ERR_UNSUPPORTED;
    }

    result = memfs_lookup_parent(new_relative_path, &new_parent_index, new_leaf_name);
    if (result != VFS_OK) {
        return result;
    }

    MemFSNode* node = memfs_get_node(node_index);
    if (node == 0) {
        return VFS_ERR_NOT_FOUND;
    }

    if (node->parent == new_parent_index && vfs_str_eq(node->name, new_leaf_name)) {
        return VFS_OK;
    }
    if (memfs_find_child(new_parent_index, new_leaf_name) >= 0) {
        return VFS_ERR_ALREADY_MOUNTED;
    }
    if (node->type == VFS_NODE_DIR &&
        (new_parent_index == node_index || memfs_is_descendant(new_parent_index, node_index))) {
        return VFS_ERR_INVALID_PATH;
    }

    if (node->parent != new_parent_index) {
        memfs_unlink_child(node->parent, node_index);
        MemFSNode* new_parent = memfs_get_node(new_parent_index);
        if (new_parent == 0 || new_parent->type != VFS_NODE_DIR) {
            return VFS_ERR_INVALID_PATH;
        }
        node->next_sibling = new_parent->first_child;
        new_parent->first_child = node_index;
        node->parent = new_parent_index;
    }

    vfs_copy_string(node->name, sizeof(node->name), new_leaf_name);
    return VFS_OK;
}

static const VFSBackendOps g_memfs_backend_ops = {
    memfs_backend_list_files,
    memfs_backend_read_dir_entry,
    memfs_backend_get_file_info,
    memfs_backend_read_file,
    memfs_backend_write_file,
    memfs_backend_touch_file,
    memfs_backend_delete_file,
    memfs_backend_mkdir,
    memfs_backend_rmdir,
    memfs_backend_rename_path,
};
