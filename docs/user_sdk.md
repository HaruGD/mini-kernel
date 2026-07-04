# OS64 User SDK v2

The user SDK is the supported C interface for OS64 ELF applications.

```c
#include <os64/os64.h>

int main(void) {
    os_printf("hello from pid %ld\n", os_getpid());
    return 0;
}
```

The build compiles `user/sdk/src/` into `build/libos64.a` and links it into every C user program. Syscall numbers and the `int 0x80` calling sequence remain private to the SDK.

## API groups

- Console: `os_printf`, `os_puts`, `os_read_line`, `os_clear`
- Strings: `os_strlen`, `os_streq`, `os_trim`, `os_parse_u32`
- Files: handle I/O plus `os_read_file`, `os_read_text_file`, `os_write_file`, `os_append_file`
- Dynamic files: `os_read_file_alloc`, `os_read_text_file_alloc`
- Directories and paths: cwd, directory iteration, create/remove/rename, normalization
- Processes: pid, run, wait, yield, tick-based sleep, uptime, and child reaping
- Memory: `os_malloc`, `os_calloc`, `os_realloc`, `os_free`, `os_strdup`
- Results: stable negative error codes and `os_result_string`
- Time: monotonic ticks, timer frequency, and milliseconds
- Graphics: GOP information, pixel, rectangle, line, bitmap blit, color-key blit, text, and clear primitives
- Input: blocking and nonblocking key/pointer events with modifiers and button state
- IPC: fixed-size message initialization, send, nonblocking receive, and blocking wait
- Services: register, find, and unregister short-lived service names
- Service manager protocol: fixed request/reply structs for `serviced_c.elf`

The kernel reserves a separate heap range for each active process slot. The SDK allocator grows and shrinks this range through the `brk` syscall, uses 16-byte alignment, splits reusable blocks, and coalesces adjacent free blocks. The current heap limit is approximately 960 KiB per process slot.

`os_read_file_alloc` and `os_read_text_file_alloc` allocate storage based on VFS file size and split transfers into chunks accepted by the kernel syscall ABI. Their practical file-size limit is the available user heap rather than a fixed stack buffer. The low-level `os_brk` API should not be mixed with SDK allocator calls in the same program.

Add a C source under `user/programs/`; the Makefile discovers it, links the SDK, emits `bin/<name>.elf`, and includes it in the FAT32 root image. Existing programs may continue using `userlib.h` while they are migrated.

Current ELF user programs are linked as fixed-address executable images, then
loaded into a per-process execution slot. The kernel also maps a temporary
link-address alias for the active process, so C constructs such as global
pointer tables, string-literal pointers, mutable global data, and BSS globals
work through syscalls and across yield/sleep/resume. This is a compatibility
layer for the current loader, not a full dynamic relocation system; PIE or
explicit ELF relocations remain future work.

The initial migration includes `uhello_c`, `uargs_c`, `upid_c`, `usleep_c`, `uyield_c`, `utouch_c`, `urm_c`, and `ucat_c`. More complex shell utilities remain on the compatibility header until their command-specific helpers move into the SDK.

## Integration test

`usdk_test.elf` automatically checks formatted output, ELF global pointer data,
mutable globals, BSS globals, allocation and reallocation, heap shrinking,
dynamic strings, file create/read/append/rename/delete, a 12 KiB multi-chunk
FAT32 transfer, directory iteration, relative paths, sleep, and yield. Run it
with:

```sh
make test-user-sdk
```

Phase 2 adds focused graphics and input regression groups:

```sh
make test-graphics
make test-input
```

Phase 3 adds the first IPC regression group:

```sh
make test-ipc
```

Service registry tests cover the fixed service identity ABI, automatic owner
cleanup, SDK wrappers, and `services` shell diagnostics:

```sh
make test-services
```

## SDK v2 examples

```c
OsTimeInfo time;
os_time_get(&time);
os_printf("uptime=%lu ms\n", time.milliseconds);

OsGraphicsInfo graphics;
if (os_gfx_get_info(&graphics) == OS_SUCCESS) {
    os_gfx_fill_rect(10, 10, 80, 30, OS_RGB(30, 180, 90));
    os_gfx_draw_line(10, 10, 89, 39, OS_RGB(255, 255, 255));
    os_gfx_draw_text(12, 44, "OS64", OS_RGB(255, 255, 255), 0, OS_GFX_TEXT_TRANSPARENT_BG);
}

OsKeyEvent event;
long result = os_key_poll(&event);
if (result == OS_ERR_WOULD_BLOCK) {
    os_puts("no key is waiting");
}

OsInputEvent input;
if (os_input_poll(&input) == OS_SUCCESS && input.type == OS_INPUT_EVENT_KEY) {
    os_printf("key=%u\n", input.data.key.keycode);
}
os_input_wait(&input);
```

Keyboard keycodes use the PS/2 set-1 code in the low byte and set bit `0x100` for extended keys. `OsKeyEvent.character` is populated for printable key-down events. The stable input ABI lives in `os64/input_types.h`: `OsKeyEvent` is the legacy keyboard-specific payload, while `OsInputEvent` is the common event envelope used by the input queue. `os_input_poll` returns `OS_ERR_WOULD_BLOCK` when no event is ready; `os_input_wait` blocks without busy-spinning until an event arrives, or returns `OS_ERR_NOT_READY` if the process loses input focus while waiting. The kernel shell `input` command reports queue capacity, queued events, delivered events, and dropped events without consuming pending input.

The compatibility `user_getchar()` path used by older `userlib.h` programs now
reads from the focused process event queue as well. This keeps `ushell_c.elf`
working while new applications move to `os_key_*` and `os_input_*`.

Pointer events are reserved in the same ABI even before a hardware mouse driver
exists. `OsPointerEvent.x` and `y` contain absolute coordinates when known, or
`OS_POINTER_POSITION_UNKNOWN` for purely relative devices. `delta_x` and
`delta_y` carry relative motion, `wheel_delta` carries vertical wheel movement,
`buttons` is the post-event pressed-button mask, and `changed_buttons` marks the
button bits that changed for button up/down events.

Graphics calls are mediated by kernel syscalls. User programs receive dimensions and pixel format but do not receive the physical framebuffer address. Rectangle drawing clips at the display boundary, while an origin outside the display returns `OS_ERR_OUT_OF_RANGE`. Zero-sized rectangles return `OS_ERR_INVALID_ARGUMENT`.

`ugfxdemo_c.elf` demonstrates the first 2D helper layer from user space. It uses
SDK helpers for lines, filled rectangles, bitmap blits, color-key blits, and
bitmap-font text. These helpers currently draw through the existing pixel and
rectangle syscalls, so they are intended for simple demos and tests until the
kernel exposes batched 2D drawing syscalls.

`uevent_c.elf` demonstrates the focused input-event path. It blocks in
`os_input_wait`, prints key events, and exits when it receives `q` or Enter.

`uping_c.elf` and `upong_c.elf` demonstrate the first IPC path. `uping` starts
`upong`, sends a request message, waits for a reply, and verifies the `pong`
payload. The kernel shell `ipc` command reports mailbox capacity, queued
messages, delivered messages, dropped messages, and wait state per process
without consuming pending messages.

`usvc_c.elf` demonstrates the service registry path. It registers the `demo`
service, verifies duplicate and invalid names are rejected, finds its own
`OsServiceInfo`, then exits without explicit unregister so the kernel cleanup
path can prove stale pids are not returned. Service names are fixed-size
lowercase identifiers up to `OS_SERVICE_NAME_MAX - 1` bytes and services remain
ordinary ELF user programs.

`serviced_c.elf` is the first user-space service manager. It registers as
`service`, receives `OsServiceManagerRequest` messages over IPC, starts known
service ELF binaries, stops/restarts owned children, and replies with
`OsServiceManagerReply`. The kernel shell `service ...` command is a thin
frontend that runs `usvcctl_c.elf`, so service policy remains in user space.
The first static dependency table starts `base` before `demo`.

The full Phase 2 closure matrix is documented in
`docs/phase2_regression_matrix.md`.

All SDK buffers are checked against the current process mappings. Kernel addresses,
another process slot, read-only code pages, and memory above the current heap break
are rejected before the kernel copies data. File helpers preserve the specific VFS
error code so callers can distinguish missing files, invalid paths, and I/O errors.
