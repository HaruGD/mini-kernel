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
    return user_syscall2(1, (long)text, (long)length);
}

static inline void user_exit(int code) {
    user_syscall1(2, (long)code);
    for (;;) {
    }
}

static inline long user_putchar(char ch) {
    return user_syscall1(3, (long)(unsigned char)ch);
}

static inline long user_getchar(void) {
    return user_syscall0(4);
}

static inline long user_clear_screen(void) {
    return user_syscall0(52);
}

static inline long user_get_pid(void) {
    return user_syscall0(15);
}

static inline long user_get_ppid(void) {
    return user_syscall0(16);
}

static inline long user_run(const char* filename) {
    return user_syscall1(7, (long)filename);
}

static inline long user_version(void) {
    return user_syscall0(8);
}

static inline long user_bootinfo(void) {
    return user_syscall0(9);
}

static inline long user_memstat(void) {
    return user_syscall0(10);
}

static inline long user_rm(const char* filename) {
    return user_syscall1(11, (long)filename);
}

static inline long user_uptime(void) {
    return user_syscall0(12);
}

static inline long user_touch(const char* filename) {
    return user_syscall1(13, (long)filename);
}

static inline long user_save(const char* filename, const char* text) {
    return user_syscall2(14, (long)filename, (long)text);
}

static inline long user_list_files(void) {
    return user_syscall0(5);
}

static inline long user_list_files_at(const char* path) {
    return user_syscall1(34, (long)path);
}

static inline long user_cat(const char* filename) {
    return user_syscall1(6, (long)filename);
}

static inline long user_ps(void) {
    return user_syscall0(17);
}

static inline long user_laststatus(void) {
    return user_syscall0(18);
}

static inline long user_wait(void) {
    return user_syscall0(19);
}

static inline long user_sched(void) {
    return user_syscall0(20);
}

static inline long user_yield(void) {
    return user_syscall0(21);
}

static inline long user_resume(long pid) {
    return user_syscall1(22, pid);
}

static inline long user_kill(long pid) {
    return user_syscall1(23, pid);
}

static inline long user_reapall(void) {
    return user_syscall0(24);
}

static inline long user_jobs(void) {
    return user_syscall0(25);
}

static inline long user_sleep(uint32_t ticks) {
    return user_syscall1(26, (long)ticks);
}

static inline long user_set_background(long pid, long enabled) {
    return user_syscall2(27, pid, enabled);
}

static inline long user_children_active(void) {
    return user_syscall0(28);
}

static inline long user_reapall_silent(void) {
    return user_syscall0(29);
}

static inline long user_rm_silent(const char* filename) {
    return user_syscall1(30, (long)filename);
}

static inline long user_touch_silent(const char* filename) {
    return user_syscall1(31, (long)filename);
}

static inline long user_save_silent(const char* filename, const char* text) {
    return user_syscall2(32, (long)filename, (long)text);
}

static inline long user_mounts(void) {
    return user_syscall0(33);
}

static inline long user_open_file(const char* path, uint32_t mode) {
    return user_syscall2(35, (long)path, (long)mode);
}

static inline long user_read_file_handle(long fd, void* buffer, uint32_t size) {
    return user_syscall3(36, fd, (long)buffer, (long)size);
}

static inline long user_write_file_handle(long fd, const void* buffer, uint32_t size) {
    return user_syscall3(37, fd, (long)buffer, (long)size);
}

static inline long user_close_file(long fd) {
    return user_syscall1(38, fd);
}

static inline long user_seek_file(long fd, int32_t offset, uint32_t whence) {
    return user_syscall3(39, fd, (long)offset, (long)whence);
}

static inline long user_tell_file(long fd) {
    return user_syscall1(40, fd);
}

static inline long user_mkdir(const char* path) {
    return user_syscall1(41, (long)path);
}

static inline long user_rmdir(const char* path) {
    return user_syscall1(42, (long)path);
}

static inline long user_mkdir_silent(const char* path) {
    return user_syscall1(49, (long)path);
}

static inline long user_rmdir_silent(const char* path) {
    return user_syscall1(50, (long)path);
}

static inline long user_rename(const char* old_path, const char* new_path) {
    return user_syscall2(51, (long)old_path, (long)new_path);
}

static inline long user_get_file_info(const char* path, UserVFSInfo* info) {
    return user_syscall2(43, (long)path, (long)info);
}

static inline long user_getcwd(char* buffer, uint32_t capacity) {
    return user_syscall2(44, (long)buffer, (long)capacity);
}

static inline long user_chdir(const char* path) {
    return user_syscall1(45, (long)path);
}

static inline long user_opendir(const char* path) {
    return user_syscall1(46, (long)path);
}

static inline long user_readdir(long fd, UserDirEntry* entry) {
    return user_syscall2(47, fd, (long)entry);
}

static inline long user_closedir(long fd) {
    return user_syscall1(48, fd);
}
