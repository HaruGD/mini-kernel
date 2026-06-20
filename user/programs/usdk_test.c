#include <os64/os64.h>

#define TEST_DIR "/mem/usdk_test"
#define TEST_TEXT_PATH TEST_DIR "/data.txt"
#define TEST_RENAMED_PATH TEST_DIR "/result.txt"
#define TEST_LARGE_PATH "/sdk_large_test.bin"
#define TEST_LARGE_SIZE 12000u

static uint32_t checks_passed = 0;
static uint32_t checks_failed = 0;

static void check(int condition, const char* name) {
    if (condition) {
        checks_passed++;
        os_printf("[PASS] %s\n", name);
    } else {
        checks_failed++;
        os_printf("[FAIL] %s\n", name);
    }
}

static int buffer_has_pattern(const uint8_t* buffer, uint32_t size) {
    for (uint32_t i = 0; i < size; i++) {
        if (buffer[i] != (uint8_t)('A' + (i % 26u))) {
            return 0;
        }
    }
    return 1;
}

static void test_allocator(void) {
    void* initial_break = os_brk(0);
    uint8_t* first = (uint8_t*)os_malloc(48);
    uint8_t* zeroed = (uint8_t*)os_calloc(64, 1);

    check(initial_break != 0, "brk query");
    check(first != 0 && zeroed != 0, "malloc and calloc");
    if (first == 0 || zeroed == 0) {
        os_free(first);
        os_free(zeroed);
        return;
    }

    for (uint32_t i = 0; i < 48; i++) {
        first[i] = (uint8_t)(i + 1u);
    }
    int calloc_zero = 1;
    for (uint32_t i = 0; i < 64; i++) {
        if (zeroed[i] != 0) {
            calloc_zero = 0;
            break;
        }
    }
    check(calloc_zero, "calloc zero fill");

    first = (uint8_t*)os_realloc(first, 8192);
    int realloc_preserved = first != 0;
    if (first != 0) {
        for (uint32_t i = 0; i < 48; i++) {
            if (first[i] != (uint8_t)(i + 1u)) {
                realloc_preserved = 0;
                break;
            }
        }
    }
    check(realloc_preserved, "realloc preserves data");

    char* duplicate = os_strdup("dynamic string");
    check(duplicate != 0 && os_streq(duplicate, "dynamic string"), "strdup");

    os_free(duplicate);
    os_free(zeroed);
    os_free(first);
    check(os_brk(0) == initial_break, "free shrinks trailing heap");
}

static void test_paths(void) {
    char old_cwd[OS_PATH_MAX];
    char resolved[OS_PATH_MAX];

    check(os_getcwd(old_cwd, sizeof(old_cwd)) == OS_OK, "getcwd");
    check(os_chdir(TEST_DIR) == OS_OK, "chdir test directory");
    check(os_resolve_path("./data.txt", resolved, sizeof(resolved)) &&
          os_streq(resolved, TEST_TEXT_PATH),
          "relative path resolution");
    check(os_chdir(old_cwd) == OS_OK, "restore cwd");
}

static void test_text_file(void) {
    static const char first[] = "alpha";
    static const char second[] = "-beta";
    uint32_t size = 0;

    check(os_write_file(TEST_TEXT_PATH, first, sizeof(first) - 1u) == (long)(sizeof(first) - 1u),
          "create and write file");
    check(os_append_file(TEST_TEXT_PATH, second, sizeof(second) - 1u) == (long)(sizeof(second) - 1u),
          "append file");

    char* text = os_read_text_file_alloc(TEST_TEXT_PATH, &size);
    check(text != 0 && size == 10u && os_streq(text, "alpha-beta"),
          "dynamic text file read");
    os_free(text);

    OsFileInfo info;
    check(os_stat(TEST_TEXT_PATH, &info) == OS_OK &&
          info.type == OS_NODE_FILE && info.size == 10u,
          "file stat");
    check(os_rename(TEST_TEXT_PATH, TEST_RENAMED_PATH) == OS_OK, "rename file");
}

static void test_large_file(void) {
    uint8_t* source = (uint8_t*)os_malloc(TEST_LARGE_SIZE);
    check(source != 0, "allocate large write buffer");
    if (source == 0) {
        return;
    }

    for (uint32_t i = 0; i < TEST_LARGE_SIZE; i++) {
        source[i] = (uint8_t)('A' + (i % 26u));
    }
    check(os_write_file(TEST_LARGE_PATH, source, TEST_LARGE_SIZE) == TEST_LARGE_SIZE,
          "multi-chunk file write");
    os_free(source);

    uint32_t size = 0;
    uint8_t* loaded = (uint8_t*)os_read_file_alloc(TEST_LARGE_PATH, &size);
    check(loaded != 0 && size == TEST_LARGE_SIZE && buffer_has_pattern(loaded, size),
          "dynamic multi-chunk file read");
    os_free(loaded);
}

static void test_directory(void) {
    long directory = os_opendir(TEST_DIR);
    int found_result = 0;
    check(directory >= 0, "open directory");
    if (directory < 0) {
        return;
    }

    OsDirEntry entry;
    long result;
    while ((result = os_readdir(directory, &entry)) > 0) {
        if (os_streq(entry.name, "result.txt")) {
            found_result = 1;
        }
    }
    check(result == 0, "directory iteration completes");
    check(found_result, "directory entry found");
    check(os_closedir(directory) == OS_OK, "close directory");
}

static void test_scheduler(void) {
    char* marker = os_strdup("alive across scheduling");
    check(marker != 0, "scheduler marker allocation");
    if (marker == 0) {
        return;
    }

    os_yield();
    check(os_streq(marker, "alive across scheduling"), "heap survives yield");
    os_sleep(1);
    check(os_streq(marker, "alive across scheduling"), "heap survives sleep");
    os_free(marker);
}

static void cleanup_test_files(void) {
    os_remove(TEST_TEXT_PATH);
    os_remove(TEST_RENAMED_PATH);
    os_remove(TEST_LARGE_PATH);
    os_rmdir(TEST_DIR);
}

int main(void) {
    os_puts("=== OS64 User SDK integration test ===");
    os_printf("format: d=%d u=%u x=%x s=%s\n", -42, 42u, 0x2Au, "ok");

    cleanup_test_files();
    check(os_mkdir(TEST_DIR) == OS_OK, "create test directory");

    test_allocator();
    test_paths();
    test_text_file();
    test_large_file();
    test_directory();
    test_scheduler();

    cleanup_test_files();
    os_printf("=== result: passed=%u failed=%u ===\n", checks_passed, checks_failed);
    return checks_failed == 0 ? 0 : 1;
}
