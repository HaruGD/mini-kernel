#include "userlib.h"

int main(int argc, char** argv) {
    UserDirEntry entry;
    long dir_fd;
    const char* path = "/";

    user_puts("=== uls_c.elf ===");
    if (argc >= 2 && argv[1] != 0 && argv[1][0] != '\0') {
        path = argv[1];
        user_printf("Listing files at %s from C userland.\n", path);
    } else {
        user_puts("Listing files from C userland.");
    }

    dir_fd = user_opendir(path);
    if (dir_fd >= 0) {
        while (1) {
            long result = user_readdir(dir_fd, &entry);
            if (result < 0) {
                user_closedir(dir_fd);
                user_printf("ls failed: %s\n", path);
                return 1;
            }
            if (result == 0) {
                break;
            }
            if (entry.type == USER_VFS_NODE_DIR) {
                user_printf("%s/\n", entry.name);
            } else {
                user_printf("%s\n", entry.name);
            }
        }
        user_closedir(dir_fd);
        return 0;
    }

    if (argc >= 2 && argv[1] != 0 && argv[1][0] != '\0') {
        if (user_list_files_at(path) < 0) {
            user_printf("ls failed: %s\n", path);
            return 1;
        }
    } else {
        user_list_files();
    }
    return 0;
}
