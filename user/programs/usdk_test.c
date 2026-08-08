#include <os64/os64.h>
#include <os64/syscall_numbers.h>

#define TEST_DIR "/mem/usdk_test"
#define TEST_TEXT_PATH TEST_DIR "/data.txt"
#define TEST_RENAMED_PATH TEST_DIR "/result.txt"
#define TEST_LARGE_PATH "/sdk_large_test.bin"
#define TEST_LARGE_SIZE 12000u
#define LEGACY_SYS_WRITE OS_SYS_WRITE

static uint32_t checks_passed = 0;
static uint32_t checks_failed = 0;
static uint32_t global_counter = 7;
static const char* global_words[] = {"alpha", "beta", "gamma"};
static char global_bss_buffer[16];
static const OsProcessIdentity read_only_identity = {0xA5A5A5A5u, 0x5A5A5A5Au};

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

static long raw_syscall2(long number, long arg1, long arg2) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg1), "S"(arg2)
        : "memory");
    return result;
}

static long raw_syscall1(long number, long arg1) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg1)
        : "memory");
    return result;
}

static long raw_syscall3(long number, long arg1, long arg2, long arg3) {
    long result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(number), "D"(arg1), "S"(arg2), "d"(arg3)
        : "memory");
    return result;
}

static void test_malformed_requests(void) {
    const long kernel_pointer = (long)0xFFFFFFFF80000000ULL;
    check(raw_syscall1(73, kernel_pointer) == OS_ERR_BAD_BUFFER,
          "IPC v2 rejects kernel output pointer");
    check(raw_syscall3(72, 1, 1, kernel_pointer) == OS_ERR_BAD_BUFFER,
          "IPC v2 rejects kernel input pointer");

    OsIpcMessageV2 output;
    OsIpcReceiveFilter filter;
    os_ipc_filter_init(&filter);
    filter.size--;
    check(os_msg_v2_recv_match(&filter, &output) == OS_ERR_BAD_BUFFER,
          "IPC filter rejects ABI size");
    os_ipc_filter_init(&filter);
    filter.flags = 0x80000000u;
    check(os_msg_v2_recv_match(&filter, &output) == OS_ERR_INVALID_ARGUMENT,
          "IPC filter rejects unknown flags");

    check(os_service_register("BadName", OS_SERVICE_FLAG_NONE) == OS_ERR_INVALID_ARGUMENT,
          "service rejects malformed name");
    check(os_service_register("fuzz", 0x80000000u) == OS_ERR_INVALID_ARGUMENT,
          "service rejects unknown flags");
    check(os_service_find("fuzz", (OsServiceInfo*)(uintptr_t)kernel_pointer) == OS_ERR_BAD_BUFFER,
          "service rejects kernel output pointer");
    check(raw_syscall2(SYS_VFS_OPEN, kernel_pointer, OS_OPEN_READ) ==
              OS_ERR_BAD_BUFFER,
          "VFS rejects kernel path pointer");
    check(raw_syscall1(OS64_SYSCALL_MAX_NUMBER + 1u, 0) == OS_ERR_UNSUPPORTED,
          "unknown syscall has explicit result");

    uint32_t partial_version = 0xA5A5A5A5u;
    check(raw_syscall2(OS_SYS_IPC_QUERY,
                       (long)(uintptr_t)&partial_version,
                       kernel_pointer) == OS_ERR_BAD_BUFFER &&
          partial_version == OS64_IPC_ABI_VERSION_V2,
          "IPC query documents partial output");
}

static void test_global_data(void) {
    check(os_streq(global_words[0], "alpha") &&
          os_streq(global_words[1], "beta") &&
          os_streq(global_words[2], "gamma"),
          "ELF global pointer data");

    global_counter += 5;
    check(global_counter == 12, "ELF mutable global data");

    for (uint32_t i = 0; i < sizeof(global_bss_buffer); i++) {
        if (global_bss_buffer[i] != 0) {
            check(0, "ELF global BSS zero fill");
            return;
        }
    }
    global_bss_buffer[0] = 'O';
    global_bss_buffer[1] = 'K';
    global_bss_buffer[2] = '\0';
    check(os_streq(global_bss_buffer, "OK"), "ELF global BSS write");
}

static void test_syscall_pointer_validation(void) {
    long result = raw_syscall2(LEGACY_SYS_WRITE, (long)0xFFFFFFFF80000000ULL, 4);
    check(result == OS_ERR_BAD_BUFFER, "legacy write rejects kernel pointer");
    check(raw_syscall2(LEGACY_SYS_WRITE, 1, 1) == OS_ERR_BAD_BUFFER,
          "write rejects unmapped user hole");
    check(raw_syscall2(LEGACY_SYS_WRITE, (long)(UINT64_MAX - 3u), 8) ==
              OS_ERR_OVERFLOW,
          "write rejects wrapping range");
    check(raw_syscall1(OS_SYS_GET_PROCESS_IDENTITY,
                       (long)(uintptr_t)&read_only_identity) ==
              OS_ERR_BAD_BUFFER &&
          read_only_identity.pid == 0xA5A5A5A5u &&
          read_only_identity.generation == 0x5A5A5A5Au,
          "output rejects read-only mapping without publication");

    uint8_t* allocation = (uint8_t*)os_malloc(8192u + 4096u);
    int cross_page_ok = allocation != 0;
    if (allocation != 0) {
        uintptr_t page = ((uintptr_t)allocation + 4095u) & ~(uintptr_t)4095u;
        char* crossing = (char*)(page + 4094u);
        crossing[0] = 'O';
        crossing[1] = 'K';
        crossing[2] = '\n';
        cross_page_ok = raw_syscall2(LEGACY_SYS_WRITE,
                                     (long)(uintptr_t)crossing,
                                     3) == 3;
        os_free(allocation);
    }
    check(cross_page_ok, "write accepts mapped cross-page input");
}

static void test_process_identity(void) {
    OsProcessIdentity identity;
    long result = os_get_process_identity(&identity);
    check(result == OS_SUCCESS &&
          identity.pid == (uint32_t)os_getpid() &&
          identity.generation != 0,
          "process identity");
}

static void test_dispatch_permission_preflight(void) {
    long launch = os_run_with_permissions("usyscall_policy_c.elf", 0);
    long status = launch == OS_SUCCESS ? os_wait() : launch;
    check(launch == OS_SUCCESS && status == 0,
          "descriptor permission preflight in restricted process");
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
    check(os_streq(os_result_string(OS_ERR_INVALID_HANDLE), "invalid handle") &&
          os_streq(os_result_string(OS_ERR_STALE_HANDLE), "stale handle") &&
          os_streq(os_result_string(OS_ERR_WRONG_HANDLE_TYPE), "wrong handle type") &&
          os_streq(os_result_string(OS_ERR_OVERFLOW), "arithmetic overflow"),
          "generated extended result strings");
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

static void test_wait_timeout(void) {
    OsIpcMessage message;
    uint64_t before = os_time_ticks();
    long result = os_msg_wait_timeout(&message, 2);
    uint64_t after = os_time_ticks();

    check(result == OS_ERR_TIMEOUT, "IPC wait timeout result");
    check(after >= before + 2u, "IPC wait timeout deadline");
    check(os_streq(os_result_string(OS_ERR_TIMEOUT), "timeout"),
          "timeout result string");
    check(os_streq(os_result_string(OS_ERR_CANCELLED), "cancelled"),
          "cancelled result string");
}

static void test_ipc_v2(void) {
    uint32_t version = 0;
    uint32_t features = 0;
    check(os_ipc_features(&version, &features) == OS_SUCCESS &&
          version == OS64_IPC_ABI_VERSION_V2 &&
          (features & OS_IPC_FEATURE_V2) != 0 &&
          (features & OS_IPC_FEATURE_CORRELATION) != 0 &&
          (features & OS_IPC_FEATURE_HANDLE_TRANSFER) != 0,
          "IPC v2 feature query");

    OsIpcMessageV2 message;
    os_msg_v2_init(&message, OS_IPC_MESSAGE_REQUEST);
    check(message.size == sizeof(OsIpcMessageV2) &&
          message.abi_version == OS64_IPC_ABI_VERSION_V2 &&
          message.type == OS_IPC_MESSAGE_REQUEST &&
          message.length == 0 &&
          message.handle_count == 0,
          "IPC v2 message init");

    OsIpcReceiveFilter filter;
    os_ipc_filter_init(&filter);
    filter.flags = OS_IPC_FILTER_TYPE | OS_IPC_FILTER_REPLY_TO;
    filter.type = OS_IPC_MESSAGE_REPLY;
    filter.reply_to = os_msg_next_request_id();
    check(filter.size == sizeof(OsIpcReceiveFilter) &&
          filter.type == OS_IPC_MESSAGE_REPLY &&
          filter.reply_to != 0,
          "IPC v2 receive filter init");

    check(os_msg_v2_recv_match(&filter, &message) == OS_ERR_WOULD_BLOCK,
          "IPC v2 receive match empty queue");
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
              OS_ERR_BAD_BUFFER,
          "graphics rejects kernel pointer");
}

static void test_surfaces(void) {
    const long kernel_pointer = (long)0xFFFFFFFF80000000ULL;
    check(OS64_SURFACE_ABI_VERSION == 1u &&
          OS_SURFACE_APPLICATION_RIGHTS ==
              (OS_HANDLE_RIGHT_READ | OS_HANDLE_RIGHT_WRITE |
               OS_HANDLE_RIGHT_MAP | OS_HANDLE_RIGHT_TRANSFER) &&
          OS_SURFACE_TRANSFER_RIGHTS ==
              (OS_HANDLE_RIGHT_READ | OS_HANDLE_RIGHT_MAP),
          "surface ABI constants");
    check(os_surface_create(0, 1, OS64_PIXEL_FORMAT_RGB) == 0,
          "surface rejects zero width");
    check(os_surface_create(UINT32_MAX, UINT32_MAX, OS64_PIXEL_FORMAT_RGB) == 0,
          "surface rejects overflowing dimensions");

    OsHandle surface = os_surface_create(1025, 1, OS64_PIXEL_FORMAT_RGB);
    check(surface != 0, "surface create");
    if (surface == 0) {
        return;
    }
    OsGraphicsSurfaceHandleInfo info;
    check(os_surface_get_info(surface, &info) == OS_SUCCESS &&
          info.width == 1025 && info.height == 1 &&
          info.stride_pixels == 1025 &&
          info.pixel_format == OS64_PIXEL_FORMAT_RGB &&
          info.byte_size == 4100,
          "surface information");
    uint8_t untouched = 0xA5u;
    check(raw_syscall3(SYS_VFS_READ, 0,
                       (long)(uintptr_t)&untouched, 1) == OS_ERR_INVALID_HANDLE &&
          untouched == 0xA5u,
          "malformed handle preserves output");
    check(raw_syscall3(SYS_VFS_READ, (long)surface,
                       (long)(uintptr_t)&untouched, 1) ==
              OS_ERR_WRONG_HANDLE_TYPE &&
          untouched == 0xA5u,
          "wrong handle type preserves output");
    check(raw_syscall2(77, (long)surface, kernel_pointer) == OS_ERR_BAD_BUFFER,
          "surface info rejects kernel pointer");
    check(raw_syscall2(78, (long)surface, 0x80000000u) ==
              OS_ERR_INVALID_ARGUMENT,
          "surface map rejects unknown flags");

    uint32_t* pixels = (uint32_t*)os_surface_map(
        surface, OS_SURFACE_MAP_READ | OS_SURFACE_MAP_WRITE);
    check(pixels != 0, "surface writable mapping");
    if (pixels != 0) {
        check(pixels[0] == 0 && pixels[1024] == 0,
              "surface mapping is zero filled across pages");
        pixels[0] = 0x00112233u;
        pixels[1024] = 0x00445566u;
        check(pixels[0] == 0x00112233u && pixels[1024] == 0x00445566u,
              "surface writable mapping crosses page boundary");
        check(os_surface_map(surface,
                             OS_SURFACE_MAP_READ | OS_SURFACE_MAP_WRITE) == pixels,
              "surface repeated map is stable");
        check(os_surface_unmap(surface, pixels + 1) == OS_ERR_INVALID_ARGUMENT,
              "surface rejects wrong unmap address");
        check(os_surface_unmap(surface, pixels) == OS_SUCCESS,
              "surface unmap");
        check(os_surface_unmap(surface, pixels) == OS_ERR_NOT_FOUND,
              "surface repeated unmap is safe");
    }
    check(os_surface_close(surface) == OS_SUCCESS, "surface close");
    check(os_surface_close(surface) == OS_ERR_STALE_HANDLE,
          "surface stale close is safe");
    os_memset(&info, 0xA5, sizeof(info));
    check(os_surface_get_info(surface, &info) == OS_ERR_STALE_HANDLE &&
          ((const uint8_t*)&info)[0] == 0xA5u,
          "stale handle preserves atomic output");
    check(os_surface_map(surface, OS_SURFACE_MAP_READ) == 0,
          "surface stale map is safe");

    surface = os_surface_create(4, 4, OS64_PIXEL_FORMAT_BGR);
    pixels = (uint32_t*)os_surface_map(surface,
                                      OS_SURFACE_MAP_READ | OS_SURFACE_MAP_WRITE);
    check(surface != 0 && pixels != 0 && os_surface_close(surface) == OS_SUCCESS,
          "surface close releases active mapping");
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

    test_global_data();
    test_syscall_pointer_validation();
    test_process_identity();
    test_dispatch_permission_preflight();
    test_allocator();
    test_paths();
    test_text_file();
    test_large_file();
    test_directory();
    test_scheduler();
    test_results();
    test_time();
    test_wait_timeout();
    test_ipc_v2();
    test_malformed_requests();
    test_surfaces();
    test_graphics();
    test_keyboard();

    cleanup_test_files();
    os_printf("=== result: passed=%u failed=%u ===\n", checks_passed, checks_failed);
    return checks_failed == 0 ? 0 : 1;
}
