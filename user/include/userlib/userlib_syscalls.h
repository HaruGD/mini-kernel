#include <os64/syscall_numbers.h>

static inline long user_syscall0(long number) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number)
        : "memory");
    return result;
}

static inline long user_syscall1(long number, long arg1) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg1)
        : "memory");
    return result;
}

static inline long user_syscall2(long number, long arg1, long arg2) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg1), "S"(arg2)
        : "memory");
    return result;
}

static inline long user_syscall3(long number, long arg1, long arg2, long arg3) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3)
        : "memory");
    return result;
}

static inline long user_write(const char* text, uint64_t length) {
    return user_syscall2(SYS_WRITE, (long)text, (long)length);
}

static inline void user_exit(int code) {
    user_syscall1(SYS_EXIT, (long)code);
    for (;;) {
    }
}

static inline long user_putchar(char ch) {
    return user_syscall1(SYS_PUTCHAR, (long)(unsigned char)ch);
}

static inline long user_getchar(void) {
    return user_syscall0(SYS_GETCHAR);
}

static inline long user_clear_screen(void) {
    return user_syscall0(SYS_CLEAR_SCREEN);
}

static inline long user_get_pid(void) {
    return user_syscall0(SYS_GET_PID);
}

static inline long user_get_ppid(void) {
    return user_syscall0(SYS_GET_PPID);
}

static inline long user_run(const char* filename) {
    return user_syscall1(SYS_RUN_USER, (long)filename);
}

static inline long user_version(void) {
    return user_syscall0(SYS_VERSION);
}

static inline long user_bootinfo(void) {
    return user_syscall0(SYS_BOOTINFO);
}

static inline long user_memstat(void) {
    return user_syscall0(SYS_MEMSTAT);
}

static inline long user_rm(const char* filename) {
    return user_syscall1(SYS_RM_FILE, (long)filename);
}

static inline long user_uptime(void) {
    return user_syscall0(SYS_UPTIME);
}

static inline long user_touch(const char* filename) {
    return user_syscall1(SYS_TOUCH_FILE, (long)filename);
}

static inline long user_save(const char* filename, const char* text) {
    return user_syscall2(SYS_SAVE_FILE, (long)filename, (long)text);
}

static inline long user_list_files(void) {
    return user_syscall0(SYS_LIST_FILES);
}

static inline long user_list_files_at(const char* path) {
    return user_syscall1(SYS_LIST_FILES_AT, (long)path);
}

static inline long user_cat(const char* filename) {
    return user_syscall1(SYS_CAT_FILE, (long)filename);
}

static inline long user_ps(void) {
    return user_syscall0(SYS_PS);
}

static inline long user_laststatus(void) {
    return user_syscall0(SYS_LAST_STATUS);
}

static inline long user_wait(void) {
    return user_syscall0(SYS_WAIT_CHILD);
}

static inline long user_sched(void) {
    return user_syscall0(SYS_SCHED_INFO);
}

static inline long user_yield(void) {
    return user_syscall0(SYS_YIELD);
}

static inline long user_resume(long pid) {
    return user_syscall1(SYS_RESUME_USER, pid);
}

static inline long user_kill(long pid) {
    return user_syscall1(SYS_KILL_USER, pid);
}

static inline long user_reapall(void) {
    return user_syscall0(SYS_REAP_ALL_CHILDREN);
}

static inline long user_jobs(void) {
    return user_syscall0(SYS_JOBS);
}

static inline long user_sleep(uint32_t ticks) {
    return user_syscall1(SYS_SLEEP, (long)ticks);
}

static inline long user_set_background(long pid, long enabled) {
    return user_syscall2(SYS_SET_BACKGROUND, pid, enabled);
}

static inline long user_children_active(void) {
    return user_syscall0(SYS_CHILDREN_ACTIVE);
}

static inline long user_reapall_silent(void) {
    return user_syscall0(SYS_REAP_ALL_CHILDREN_SILENT);
}

static inline long user_rm_silent(const char* filename) {
    return user_syscall1(SYS_RM_FILE_SILENT, (long)filename);
}

static inline long user_touch_silent(const char* filename) {
    return user_syscall1(SYS_TOUCH_FILE_SILENT, (long)filename);
}

static inline long user_save_silent(const char* filename, const char* text) {
    return user_syscall2(SYS_SAVE_FILE_SILENT, (long)filename, (long)text);
}

static inline long user_mounts(void) {
    return user_syscall0(SYS_VFS_MOUNTS);
}

static inline long user_open_file(const char* path, uint32_t mode) {
    return user_syscall2(SYS_VFS_OPEN, (long)path, (long)mode);
}

static inline long user_read_file_handle(long fd, void* buffer, uint32_t size) {
    return user_syscall3(SYS_VFS_READ, fd, (long)buffer, (long)size);
}

static inline long user_write_file_handle(long fd, const void* buffer, uint32_t size) {
    return user_syscall3(SYS_VFS_WRITE, fd, (long)buffer, (long)size);
}

static inline long user_close_file(long fd) {
    return user_syscall1(SYS_VFS_CLOSE, fd);
}

static inline long user_seek_file(long fd, int32_t offset, uint32_t whence) {
    return user_syscall3(SYS_VFS_SEEK, fd, (long)offset, (long)whence);
}

static inline long user_tell_file(long fd) {
    return user_syscall1(SYS_VFS_TELL, fd);
}

static inline long user_mkdir(const char* path) {
    return user_syscall1(SYS_MKDIR, (long)path);
}

static inline long user_rmdir(const char* path) {
    return user_syscall1(SYS_RMDIR, (long)path);
}

static inline long user_mkdir_silent(const char* path) {
    return user_syscall1(SYS_MKDIR_SILENT, (long)path);
}

static inline long user_rmdir_silent(const char* path) {
    return user_syscall1(SYS_RMDIR_SILENT, (long)path);
}

static inline long user_rename(const char* old_path, const char* new_path) {
    return user_syscall2(SYS_RENAME_PATH, (long)old_path, (long)new_path);
}

static inline long user_get_file_info(const char* path, UserVFSInfo* info) {
    return user_syscall2(SYS_VFS_INFO, (long)path, (long)info);
}

static inline long user_getcwd(char* buffer, uint32_t capacity) {
    return user_syscall2(SYS_GETCWD, (long)buffer, (long)capacity);
}

static inline long user_chdir(const char* path) {
    return user_syscall1(SYS_CHDIR, (long)path);
}

static inline long user_opendir(const char* path) {
    return user_syscall1(SYS_VFS_OPENDIR, (long)path);
}

static inline long user_readdir(long fd, UserDirEntry* entry) {
    return user_syscall2(SYS_VFS_READDIR, fd, (long)entry);
}

static inline long user_closedir(long fd) {
    return user_syscall1(SYS_VFS_CLOSEDIR, fd);
}
