#include <os64/os64.h>

int main(int argc, char** argv) {
    char cwd[OS_PATH_MAX];

    if (argc == 2 && os_streq(argv[1], "surface-leak")) {
        OsHandle surface = os_surface_create(1025, 1, OS64_PIXEL_FORMAT_RGB);
        uint32_t* pixels = surface != 0
            ? (uint32_t*)os_surface_map(
                  surface, OS_SURFACE_MAP_READ | OS_SURFACE_MAP_WRITE)
            : 0;
        if (pixels == 0) {
            os_puts("[usurface-leak] create/map failed");
            return 1;
        }
        pixels[0] = 0x00112233u;
        pixels[1024] = 0x00445566u;
        os_puts("[usurface-leak] mapped exit");
        return 0;
    }

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
