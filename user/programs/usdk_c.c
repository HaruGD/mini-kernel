#include <os64/os64.h>

int main(int argc, char** argv) {
    char cwd[OS_PATH_MAX];

    os_puts("=== OS64 User SDK v1 ===");
    os_printf("pid=%ld ppid=%ld argc=%d\n", os_getpid(), os_getppid(), argc);

    if (os_getcwd(cwd, sizeof(cwd)) == OS_OK) {
        os_printf("cwd=%s\n", cwd);
    }

    for (int i = 0; i < argc; i++) {
        os_printf("argv[%d]=%s\n", i, argv[i]);
    }

    os_puts("console, path, file, directory, and process APIs are ready.");
    return 0;
}
