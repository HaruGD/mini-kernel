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

static void test_results(void) {
    char long_path[OS_PATH_MAX + 16u];
    OsFileInfo info;

    check(os_result_failed(OS_ERR_NOT_FOUND), "error result detection");
    check(os_streq(os_result_string(OS_ERR_NOT_FOUND), "not found"),
          "error result string");
    check(os_streq(os_result_string(-999), "unknown error"),
          "unknown error string");
    check(os_open("/mem/usdk_test/missing.txt", OS_OPEN_READ) == OS_ERR_NOT_FOUND,
          "filesystem error propagation");
    check(os_read_file(0, 0, 0) == OS_ERR_INVALID_ARGUMENT,
          "helper argument error");
    os_memset(long_path, 'a', sizeof(long_path));
    long_path[sizeof(long_path) - 1u] = '\0';
    check(os_stat(long_path, &info) == OS_ERR_INVALID_ARGUMENT,
          "overlong path rejected");
}

static void test_time(void) {
    OsTimeInfo before;
    OsTimeInfo after;
    os_time_get(&before);
    os_sleep(2);
    os_time_get(&after);

    check(before.frequency == 100u, "time frequency");
    check(after.ticks >= before.ticks + 2u, "monotonic ticks across sleep");
    check(after.milliseconds >= before.milliseconds, "monotonic milliseconds");
    uint64_t current_ms = os_time_milliseconds();
    check(current_ms >= after.milliseconds,
          "time conversion consistency");
}

static void test_graphics(void) {
    OsGraphicsInfo info;
    long result = os_gfx_get_info(&info);
    check(result == OS_SUCCESS && info.width > 0 && info.height > 0,
          "graphics information");
    if (result < 0 || info.width == 0 || info.height == 0) {
        return;
    }

    uint32_t x = info.width > 12u ? info.width - 12u : 0u;
    uint32_t y = info.height > 12u ? info.height - 12u : 0u;
    check(os_gfx_fill_rect(x, y, 8, 8, OS_RGB(30, 180, 90)) == OS_SUCCESS,
          "graphics fill rectangle");
    check(os_gfx_put_pixel(x, y, OS_RGB(255, 255, 255)) == OS_SUCCESS,
          "graphics put pixel");
    check(os_gfx_put_pixel(info.width, info.height, 0) == OS_ERR_OUT_OF_RANGE,
          "graphics bounds error");
    check(os_gfx_fill_rect(x, y, 0, 1, 0) == OS_ERR_INVALID_ARGUMENT,
          "graphics empty rectangle error");
    check(os_gfx_fill_rect(info.width - 1u, info.height - 1u,
                           UINT32_MAX, UINT32_MAX, OS_RGB(30, 180, 90)) == OS_SUCCESS,
          "graphics overflow-safe clipping");
    check(os_gfx_get_info((OsGraphicsInfo*)(uintptr_t)0x100000u) ==
              OS_ERR_INVALID_ARGUMENT,
          "graphics rejects kernel pointer");
}

static void test_keyboard(void) {
    OsKeyEvent event;
    OsInputEvent input_event;
    OsPointerEvent pointer_event;
    check(sizeof(event) == 16u &&
          sizeof(pointer_event) == 32u &&
          sizeof(input_event) == 48u,
          "input ABI sizes");
    pointer_event.type = OS_POINTER_EVENT_BUTTON_DOWN;
    pointer_event.x = OS_POINTER_POSITION_UNKNOWN;
    pointer_event.y = OS_POINTER_POSITION_UNKNOWN;
    pointer_event.delta_x = -2;
    pointer_event.delta_y = 3;
    pointer_event.wheel_delta = 0;
    pointer_event.buttons = OS_POINTER_BUTTON_LEFT | OS_POINTER_BUTTON_X1;
    pointer_event.changed_buttons = OS_POINTER_BUTTON_LEFT;
    check(pointer_event.x == OS_POINTER_POSITION_UNKNOWN &&
          pointer_event.buttons == (OS_POINTER_BUTTON_LEFT | OS_POINTER_BUTTON_X1) &&
          pointer_event.changed_buttons == OS_POINTER_BUTTON_LEFT,
          "pointer ABI semantics");
    check(os_input_poll(0) == OS_ERR_INVALID_ARGUMENT,
          "input poll null event error");
    long input_result = os_input_poll(&input_event);
    int valid_input_event = input_result == OS_SUCCESS &&
        input_event.type == OS_INPUT_EVENT_KEY &&
        input_event.size == sizeof(OsInputEvent) &&
        (input_event.data.key.type == OS_KEY_EVENT_DOWN ||
         input_event.data.key.type == OS_KEY_EVENT_UP);
    check(input_result == OS_ERR_WOULD_BLOCK || valid_input_event,
          "input nonblocking poll");
    for (uint32_t i = 0; i < 256u; i++) {
        if (os_input_poll(&input_event) == OS_ERR_WOULD_BLOCK) {
            break;
        }
    }
    check(os_key_poll(0) == OS_ERR_INVALID_ARGUMENT,
          "keyboard null event error");
    long result = os_key_poll(&event);
    int valid_pending_event = result == OS_SUCCESS &&
        (event.type == OS_KEY_EVENT_DOWN || event.type == OS_KEY_EVENT_UP);
    check(result == OS_ERR_WOULD_BLOCK || valid_pending_event,
          "keyboard nonblocking poll");
    for (uint32_t i = 0; i < 256u; i++) {
        if (os_input_poll(&input_event) == OS_ERR_WOULD_BLOCK) {
            break;
        }
    }

    os_puts("[INFO] waiting for injected input event");
    result = os_input_wait(&input_event);
    check(result == OS_SUCCESS, "input blocking event");
    check(input_event.type == OS_INPUT_EVENT_KEY &&
          input_event.data.key.type == OS_KEY_EVENT_DOWN &&
          input_event.data.key.character == 'z',
          "input key-down payload");

    os_puts("[INFO] waiting for injected key event");
    int found_key = 0;
    for (uint32_t i = 0; i < 8u && !found_key; i++) {
        result = os_key_wait(&event);
        if (result == OS_SUCCESS &&
            event.type == OS_KEY_EVENT_DOWN &&
            event.character == 'x') {
            found_key = 1;
        }
    }
    check(found_key, "keyboard blocking event payload");
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
    test_results();
    test_time();
    test_graphics();
    test_keyboard();

    cleanup_test_files();
    os_printf("=== result: passed=%u failed=%u ===\n", checks_passed, checks_failed);
    return checks_failed == 0 ? 0 : 1;
}
