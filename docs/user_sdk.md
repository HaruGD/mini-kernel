# OS64 User SDK v1

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

The kernel reserves a separate heap range for each active process slot. The SDK allocator grows and shrinks this range through the `brk` syscall, uses 16-byte alignment, splits reusable blocks, and coalesces adjacent free blocks. The current heap limit is approximately 960 KiB per process slot.

`os_read_file_alloc` and `os_read_text_file_alloc` allocate storage based on VFS file size and split transfers into chunks accepted by the kernel syscall ABI. Their practical file-size limit is the available user heap rather than a fixed stack buffer. The low-level `os_brk` API should not be mixed with SDK allocator calls in the same program.

Add a C source under `user/programs/`; the Makefile discovers it, links the SDK, emits `bin/<name>.elf`, and includes it in the FAT32 root image. Existing programs may continue using `userlib.h` while they are migrated.

The initial migration includes `uhello_c`, `uargs_c`, `upid_c`, `usleep_c`, `uyield_c`, `utouch_c`, `urm_c`, and `ucat_c`. More complex shell utilities remain on the compatibility header until their command-specific helpers move into the SDK.

## Integration test

`usdk_test.elf` automatically checks formatted output, allocation and reallocation, heap shrinking, dynamic strings, file create/read/append/rename/delete, a 12 KiB multi-chunk FAT32 transfer, directory iteration, relative paths, sleep, and yield. Run it with:

```sh
make test-user-sdk
```
