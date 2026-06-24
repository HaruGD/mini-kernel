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
- Input: blocking and nonblocking keyboard events with modifiers

The kernel reserves a separate heap range for each active process slot. The SDK allocator grows and shrinks this range through the `brk` syscall, uses 16-byte alignment, splits reusable blocks, and coalesces adjacent free blocks. The current heap limit is approximately 960 KiB per process slot.

`os_read_file_alloc` and `os_read_text_file_alloc` allocate storage based on VFS file size and split transfers into chunks accepted by the kernel syscall ABI. Their practical file-size limit is the available user heap rather than a fixed stack buffer. The low-level `os_brk` API should not be mixed with SDK allocator calls in the same program.

Add a C source under `user/programs/`; the Makefile discovers it, links the SDK, emits `bin/<name>.elf`, and includes it in the FAT32 root image. Existing programs may continue using `userlib.h` while they are migrated.

The initial migration includes `uhello_c`, `uargs_c`, `upid_c`, `usleep_c`, `uyield_c`, `utouch_c`, `urm_c`, and `ucat_c`. More complex shell utilities remain on the compatibility header until their command-specific helpers move into the SDK.

## Integration test

`usdk_test.elf` automatically checks formatted output, allocation and reallocation, heap shrinking, dynamic strings, file create/read/append/rename/delete, a 12 KiB multi-chunk FAT32 transfer, directory iteration, relative paths, sleep, and yield. Run it with:

```sh
make test-user-sdk
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
```

Keyboard keycodes use the PS/2 set-1 code in the low byte and set bit `0x100` for extended keys. `OsKeyEvent.character` is populated for printable key-down events. The current API is suitable for the PS/2 driver and can preserve the same public structure when USB HID input is added later.

Graphics calls are mediated by kernel syscalls. User programs receive dimensions and pixel format but do not receive the physical framebuffer address. Rectangle drawing clips at the display boundary, while an origin outside the display returns `OS_ERR_OUT_OF_RANGE`. Zero-sized rectangles return `OS_ERR_INVALID_ARGUMENT`.

`ugfxdemo_c.elf` demonstrates the first 2D helper layer from user space. It uses
SDK helpers for lines, filled rectangles, bitmap blits, color-key blits, and
bitmap-font text. These helpers currently draw through the existing pixel and
rectangle syscalls, so they are intended for simple demos and tests until the
kernel exposes batched 2D drawing syscalls.

All SDK buffers are checked against the current process mappings. Kernel addresses,
another process slot, read-only code pages, and memory above the current heap break
are rejected before the kernel copies data. File helpers preserve the specific VFS
error code so callers can distinguish missing files, invalid paths, and I/O errors.
