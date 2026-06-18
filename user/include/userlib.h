#ifndef USERLIB_H
#define USERLIB_H

#include <stdarg.h>
#include <stdint.h>

#define USER_VFS_OPEN_READ     0x00000001u
#define USER_VFS_OPEN_WRITE    0x00000002u
#define USER_VFS_OPEN_CREATE   0x00000004u
#define USER_VFS_OPEN_TRUNCATE 0x00000008u
#define USER_VFS_OPEN_APPEND   0x00000010u

#define USER_VFS_SEEK_SET 0u
#define USER_VFS_SEEK_CUR 1u
#define USER_VFS_SEEK_END 2u

#define USER_VFS_NODE_NONE 0u
#define USER_VFS_NODE_FILE 1u
#define USER_VFS_NODE_DIR  2u

typedef struct UserVFSInfo {
    uint32_t type;
    uint32_t size;
} UserVFSInfo;

typedef struct UserDirEntry {
    uint32_t type;
    uint32_t size;
    char name[64];
} UserDirEntry;

#include "userlib/userlib_syscalls.h"
#include "userlib/userlib_text.h"
#include "userlib/userlib_path_input.h"

#endif
