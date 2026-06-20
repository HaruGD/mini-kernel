#ifndef OS64_OS64_H
#define OS64_OS64_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#define OS64_SDK_VERSION_MAJOR 1u
#define OS64_SDK_VERSION_MINOR 0u
#define OS_OK 0
#define OS_ERROR (-1)
#define OS_PATH_MAX 160u
#define OS_NAME_MAX 64u

#define OS_OPEN_READ 0x01u
#define OS_OPEN_WRITE 0x02u
#define OS_OPEN_CREATE 0x04u
#define OS_OPEN_TRUNCATE 0x08u
#define OS_OPEN_APPEND 0x10u
#define OS_SEEK_SET 0u
#define OS_SEEK_CUR 1u
#define OS_SEEK_END 2u
#define OS_NODE_NONE 0u
#define OS_NODE_FILE 1u
#define OS_NODE_DIR 2u

typedef struct OsFileInfo {
    uint32_t type;
    uint32_t size;
} OsFileInfo;

typedef struct OsDirEntry {
    uint32_t type;
    uint32_t size;
    char name[OS_NAME_MAX];
} OsDirEntry;

size_t os_strlen(const char* text);
int os_streq(const char* left, const char* right);
char* os_trim(char* text);
int os_parse_u32(const char* text, uint32_t* value_out);
void* os_memset(void* destination, int value, size_t size);
void* os_memcpy(void* destination, const void* source, size_t size);
char* os_strdup(const char* text);

void* os_brk(void* address);
void* os_malloc(size_t size);
void os_free(void* pointer);
void* os_calloc(size_t count, size_t size);
void* os_realloc(void* pointer, size_t size);

long os_write(const char* text, size_t length);
long os_putchar(char ch);
long os_getchar(void);
long os_clear(void);
long os_puts(const char* text);
void os_vprintf(const char* format, va_list args);
void os_printf(const char* format, ...);
size_t os_read_line(char* buffer, size_t capacity);

long os_open(const char* path, uint32_t mode);
long os_read(long fd, void* buffer, uint32_t size);
long os_write_fd(long fd, const void* buffer, uint32_t size);
long os_close(long fd);
long os_seek(long fd, int32_t offset, uint32_t whence);
long os_tell(long fd);
long os_stat(const char* path, OsFileInfo* info);
long os_getcwd(char* buffer, uint32_t capacity);
long os_chdir(const char* path);
long os_mkdir(const char* path);
long os_rmdir(const char* path);
long os_remove(const char* path);
long os_rename(const char* old_path, const char* new_path);
long os_touch(const char* path);
long os_opendir(const char* path);
long os_readdir(long fd, OsDirEntry* entry);
long os_closedir(long fd);
long os_read_file(const char* path, void* buffer, uint32_t capacity);
long os_read_text_file(const char* path, char* buffer, uint32_t capacity);
long os_write_file(const char* path, const void* data, uint32_t size);
long os_append_file(const char* path, const void* data, uint32_t size);
void* os_read_file_alloc(const char* path, uint32_t* size_out);
char* os_read_text_file_alloc(const char* path, uint32_t* size_out);

int os_normalize_path(const char* cwd, const char* input, char* output, uint32_t capacity);
int os_resolve_path(const char* input, char* output, uint32_t capacity);

void os_exit(int code) __attribute__((noreturn));
long os_getpid(void);
long os_getppid(void);
long os_run(const char* command_line);
long os_wait(void);
long os_yield(void);
long os_sleep(uint32_t ticks);
long os_uptime(void);
long os_reap_children(void);
#endif
