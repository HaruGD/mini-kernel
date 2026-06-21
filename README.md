# mini-kernel

Experimental x86 OS project for learning low-level systems work by building a 64-bit kernel, a small C-based userland, a VFS stack, and a UEFI boot path.

This project is developed by a non-professional developer with heavy AI assistance for learning and experimentation.

The current active path is:

```text
UEFI firmware -> BOOTX64.EFI -> kernel64.bin + OS64.BIN ramdisk -> FAT32 root / -> OS64 shell/userland
```

Legacy BIOS/32-bit/FAT12 code is archived under `archive/legacy-bios/` for reference only and is not part of the active build.

## Current Status

What works on the active 64-bit UEFI path:

- UEFI bootloader image generation
- UEFI framebuffer handoff
- `BootInfo` handoff with memory map and reserved ranges
- 64-bit long mode kernel entry
- PMM, heap, and runtime paging helpers
- PMM allocation statistics with next-fit single-page allocation hint
- paging range helpers for identity, MMIO, allocated, remapped, and freed ranges
- kernel heap block guards, invalid-free and double-free diagnostics
- kernel heap O(1) current/peak used accounting
- kernel heap immediate adjacent-block coalescing with a doubly linked block list
- kernel heap segregated free-list bins for faster free-block search
- page-based user buffer validation for syscall copy helpers
- kernel stack supplied by the UEFI loader
- IDT, GDT/TSS, PIT, and keyboard IRQ handling
- framebuffer terminal with an internal text-cell buffer
- syscall path through `int 0x80`
- process table and scheduler prototype
- bounded child-result history (`PROCESS_CHILD_RESULT_HISTORY_LIMIT=3`)
- cooperative `yield`
- timer-based preemption
- PIT tick based sleep / wakeup (`PIT_DEFAULT_HZ=100`, about 10ms per tick)
- foreground / background job control
- child-result reaping with `wait` / `reapall` plus automatic cleanup when process slots are exhausted
- ELF user program loading
- C user programs with `main(void)` and `main(argc, argv)` support
- User SDK v2 with console, string, path, file, directory, process, time, graphics, and keyboard-event APIs
- User syscall buffers constrained to process-owned mappings and page permissions
- static `libos64.a` linked into C user programs
- per-process-slot user heap backed by syscall `brk` page mapping
- SDK `malloc`, `calloc`, `realloc`, `free`, `strdup`, and dynamic file-read helpers
- automated `usdk_test.elf` integration test covering heap, files, paths, directories, sleep, and yield
- stable SDK result codes with readable error names
- monotonic 64-bit PIT time exposed to user programs
- syscall-mediated GOP drawing without exposing the physical framebuffer
- blocking and nonblocking PS/2 keyboard event delivery with modifier state
- default C shell userland: `ushell_c.elf`
- VFS layer
- FAT32 root filesystem mounted at `/`
- memory-backed filesystem mounted at `/mem`
- case-sensitive FAT32 path lookup
- handle-based file I/O:
  - `open`
  - `read`
  - `write`
  - `close`
  - `seek`
  - `tell`
- kernel driver manager prototype
- PCI discovery and device listing
- `.drv` package validation and loading
- ELF object based `.drv` builder for C and restricted freestanding C++ kernel drivers
- C ABI driver boundary with optional C/C++ internal implementation
- manifest-based external driver projects under `drivers/external/`
- separate unsigned builder and `.drv` signer
- signature ABI v1 with `LOCAL_TEST`, `ROOT_KEY`, and `TPM_LOCAL` algorithm slots
- local test signature validation for `.drv` packages
- kernel export/import resolution
- driver-to-driver export/import resolution
- manifest dependency table resolution
- bounded `.drv` load state transitions with failed/rejected diagnostics
- module export rollback when `.drv` load or entry execution fails
- `.drv` unload/reload commands with dependent-driver unload protection
- kernel hardware exports for PCI config I/O, MMIO32, VFS handles, and ATA sector I/O
- kernel display exports for GOP framebuffer info and drawing primitives
- driver PCI probe/bind registry
- driver IRQ hook registry for PIC IRQ lines
- page-separated `.drv` code/data/BSS loading with executable code pages and NX data pages
- stricter `.drv` ABI validation for arch, table shape, permissions, boot modes, sections, symbols, imports, exports, relocations, and signatures
- boot-time `.drv` autoload from the FAT32 root with manifest `NO_AUTOLOAD` support
- `hello.drv`, `hello_c.drv`, `hello_cpp.drv`, `provider_c.drv`, `consumer_c.drv`, `pci_probe_c.drv`, and `irq_timer_c.drv` entry execution
- manual display demo loading through `gop_demo_c.drv`
- Driver ABI reference: [docs/driver_abi.md](docs/driver_abi.md)
- User SDK reference: [docs/user_sdk.md](docs/user_sdk.md)

## Build

Active 64-bit build:

```sh
make all64
make uefi
```

Run the User SDK integration test in an isolated QEMU instance:

```sh
make test-user-sdk
```

Equivalent default:

```sh
make all
```

Convenience wrapper:

```sh
./build.sh
```

Build artifacts are written under `bin/` and `build/`.
Runtime logs and smoke-test captures are written under `logs/`.

Important active artifacts:

- `bin/kernel64.bin`
- `bin/os64.bin`
  FAT32 root filesystem image for the active OS path.
- `bin/uefi_esp.img`
  UEFI boot image containing `BOOTX64.EFI`, `kernel64.bin`, and `OS64.BIN`.
- `bin/hello.drv`
  Hand-built test driver package loaded from the FAT32 root filesystem.
- `bin/hello_c.drv`, `bin/provider_c.drv`, `bin/consumer_c.drv`
  C driver packages produced from `drivers/external/*/driver.c`, `driver.json`, and the separate signer.
- `bin/hello_cpp.drv`
  Minimal C++ driver package produced from `drivers/external/hello_cpp/driver.cpp`.
- `bin/gop_demo_c.drv`
  Minimal display-permission driver that draws through the kernel GOP exports.
- `bin/irq_timer_c.drv`
  Minimal interrupt-permission driver that registers an IRQ0 hook.

## Run

Run the active UEFI path:

```sh
./run.sh
```

Equivalent helper:

```sh
./run_uefi.sh
```

`run.sh` calls `build.sh`, embeds the FAT32 root image into the UEFI image as `OS64.BIN`, refreshes OVMF vars, and starts QEMU from that single boot image. `run_uefi.sh` is a compatibility wrapper around `run.sh`.

Legacy helpers such as `run32.sh`, `run64.sh`, and `reset.sh` are disabled on the active path. The active run path is UEFI-only.

Run a more real-hardware-like QEMU profile:

```sh
./run_realish.sh
```

This uses Q35, 8GB RAM, xHCI USB, e1000e networking, UEFI, and separate logs under `logs/serial_q35_8g.log` and `logs/qemu_q35_8g.log`.

The current OS keyboard driver is PS/2-based, so this profile keeps QEMU's default PS/2 keyboard path by default. Use `REALISH_USB_KBD=1 ./run_realish.sh` only when testing future USB HID keyboard support.

## Boot Layout

The active UEFI path uses one boot image:

```text
uefi_esp.img
├── BOOTX64.EFI
├── KERNEL.BIN
└── OS64.BIN
```

`BOOTX64.EFI` loads `KERNEL.BIN` to the kernel load address, loads `OS64.BIN` into RAM, records it in `BootInfo`, exits UEFI boot services, and jumps to the 64-bit kernel. The kernel mounts that RAM-backed `OS64.BIN` FAT32 image as `/`.

This keeps QEMU, VirtualBox, and USB boot experiments simple: firmware only needs to boot the UEFI image, and the early kernel no longer needs a second disk controller just to find `/`.

## Shells

There are two shell layers.

### Kernel Shell

Prompt:

```text
OS64>
```

Common commands:

- `help`
- `clear`
- `version`
- `bootinfo`
- `memmap`
- `memstat`
- `dump`
- `sched`
- `drivers`
- `bindings`
- `irqhooks`
- `pci`
- `gop [clear|test]`
- `drvinfo [path]`
- `drvcheck [path]`
- `drvload [path]`
- `drvautoload [dir]`
- `mounts`
- `ls [path]`
- `load [file]`
- `save [file]`
- `rm [file]`
- `mkdir [path]`
- `rmdir [path]`
- `run [file]`
- `resume`
- `usertest`
- `ushell`
- `ushellc`
- `uptime`

### User Shell

Prompt:

```text
csh>
```

The default C user shell is `ushell_c.elf`.

Main built-ins:

- `help`, `?`, `about`, `exit`, `clear`, `cls`
- `version`, `uptime`, `jobs`, `ps`, `wait`, `laststatus`, `reapall`
- `pwd`, `cd [path]`
- `ls [path]`, `cat [file]`, `touch [file]`, `save [file] [text]`, `rm [file]`
- `mkdir [path]`, `rmdir [path]`
- `rename [old] [new]`, `mv [old] [new]`
- `pid`, `ppid`
- `run [file]`
- `sleep [ticks]`
- `yield`
- `resume [pid]`
- `kill [pid]`
- `bg [pid]`
- `fg [pid]`
- `echo [text]`
- `tools`, `builtins`, `where [command]`

Shell shortcuts run standalone userland tools such as:

- `sched`
- `memstat`
- `bootinfo`
- `mounts`

Time commands use PIT ticks. The current PIT default is 100Hz, so one tick is about 10ms. For example, `sleep 100` sleeps for about 1000ms.

## Memory Management

The active kernel memory path is intentionally simple but instrumented:

- PMM uses a bitmap allocator with a next-free hint for single-page allocations.
- Kernel heap pages are mapped `RW | GLOBAL | NX`.
- Heap blocks carry a magic value, requested size, next/prev links, and free state.
- `kfree()` coalesces only adjacent `prev`/`next` blocks instead of scanning the full heap.
- Heap current and peak used bytes are maintained as counters.
- `kmalloc()` searches segregated free-list bins before growing the heap.
- Heap growth zeroes new regions with 64-bit stores.
- `memstat` reports PMM and heap counters, allocation failures, largest free block, invalid frees, and double frees.

The main remaining heap performance limit is that very large or fragmented bins can still require a short linear scan inside a bin. A future allocator pass can tune bin sizes or add best-fit trees for large blocks.

## User Programs

The project uses C/ELF user programs on the active path.

Examples:

- `uhello_c.elf`
- `uinfo_c.elf`
- `usleep_c.elf`
- `uyield_c.elf`
- `ubusy_c.elf`
- `ufault_c.elf`
- `uargs_c.elf`
- `uio_c.elf`

Standalone file/status tools:

- `uls_c.elf`
- `umkdir_c.elf`
- `urmdir_c.elf`
- `utouch_c.elf`
- `usave_c.elf`
- `ucat_c.elf`
- `urm_c.elf`
- `upid_c.elf`
- `uschd_c.elf`
- `umem_c.elf`
- `uvers_c.elf`
- `uboot_c.elf`
- `umnts_c.elf`

Example session:

```text
OS64> ushellc
csh> pwd
/
csh> ls /
kernel64.bin
ushell_c.elf
hello.drv
hello.txt
csh> mkdir /mem/docs
csh> cd /mem/docs
csh> save note.txt hello cwd
csh> cat note.txt
hello cwd
csh> run uargs_c.elf alpha beta gamma
```

## Filesystems

Current active VFS mounts:

- `/` -> FAT32 backend
- `/mem` -> memfs backend

FAT32 currently supports:

- root directory listing
- subdirectory listing
- file info lookup
- file read
- small file create/write/delete
- directory create/remove
- directory growth
- multi-cluster file writes
- LFN read/listing
- LFN create/write/delete
- file rename
- directory rename/move
- moved directory internal file access
- case-sensitive path lookup

Case-sensitive path example:

```text
csh> umkdir BOOt
Created dir: /BOOt
csh> cd boot
cd failed.
csh> cd BOOt
csh> pwd
/BOOt
```

FAT32 still stores internal short aliases as required by the FAT32 LFN format, but the user-visible VFS path surface is lowercase/LFN-oriented and case-sensitive.

`memfs` supports:

- nested directories
- `cwd`
- `cd`
- relative paths
- `.` and `..` normalization
- `opendir`
- `readdir`
- `closedir`

## Driver Manager

The driver manager is an early kernel-driver runtime.

Driver language policy:

- the kernel-to-driver boundary is C ABI
- lifecycle/probe/export symbols must be unmangled C symbols
- driver internals may be C or restricted freestanding C++
- C++ drivers must not depend on exceptions, RTTI, STL, global constructors, or thread-safe static initialization

Built-in drivers currently registered:

- `ata0`
- `fat32`
- `gop`
  UEFI GOP framebuffer display service. It is marked `ready` only when the UEFI framebuffer handoff is valid.
- `keyboard`
- `pit`

Kernel exports currently available:

- `kernel.klog`
- `kernel.kmalloc`
- `kernel.kfree`
- `kernel.gop_get_info`
- `kernel.gop_clear`
- `kernel.gop_putpixel`
- `kernel.gop_fill_rect`
- PCI config, BAR, and enable helpers
- PCI bind helper
- IRQ register/unregister helpers
- MMIO32 helpers
- VFS handle helpers
- ATA sector helpers

Supported commands:

```text
OS64> drivers
OS64> bindings
OS64> irqhooks
OS64> drvinfo consumer_c.drv
OS64> drvcheck hello.drv
OS64> drvload hello.drv
OS64> drvautoload /
```

Expected `hello.drv` flow:

```text
OS64> drvload hello.drv
[drv] hello.drv driver_entry()
DRV load OK result=ok file=hello.drv
...
[0x00000004] hello kind=module state=ready perms=-
```

Implemented pieces:

- `.drv` header validation
- manifest validation
- arch, boot-mode, permission, table, and relocation shape validation
- manifest dependency table resolution
- local test signature v0 validation
- unsigned builder output rejected by the kernel
- reserved signature algorithms for future root-key and TPM-local signing
- section loading
- section loading into a dedicated driver virtual address range
- import resolution
- manifest-declared import permission checks
- `DISPLAY`-permission GOP imports through `gop_demo_c.drv`
- import patching into loaded sections
- ABS64 and REL32 relocation application
- `driver_entry()` invocation
- driver export-table registration
- driver-to-driver import/export calls
- optional `driver_probe_pci()` probing
- PCI device bind registry
- IRQ hook register/unregister and dispatch
- boot-time `.drv` autoload with retry passes for dependency ordering
- driver state transition to `ready`
- unload/reload with dependent-driver protection
- detailed load diagnostics for failed import/relocation/signature paths
- page-level code/data permissions for loaded `.drv` packages
- `new/delete` use for loader metadata

Not implemented yet:

- real asymmetric cryptographic signature verification
- actual TPM hardware command path
- explicit dependency metadata and dependency-sorted loading
- full stop lifecycle for real hardware devices
- isolation from kernel address space

## Repo Layout

- `boot/uefi/`
  UEFI loader, PE/COFF link script, and kernel entry bridge.
- `arch/x86_64/`
  Long mode entry, interrupt stubs, PMM, paging, GDT/TSS, and IDT.
- `kernel/`
  64-bit kernel runtime and orchestration.
- `kernel/core/`
  Split kernel64 initialization, diagnostics, IRQ, process, and user-mode logic.
- `kernel/driver/`
  Driver manager, loader, exports, built-ins, and shell commands.
- `kernel/pci/`, `kernel/process/`, `kernel/syscall/`, `kernel/shell/`, `kernel/memory/`, `kernel/util/`
  Kernel subsystems split by responsibility.
- `drivers/builtin/`
  Terminal, GOP display, keyboard, PIT, and ATA built into the kernel.
- `drivers/external/`
  External C/C++ driver projects, SDK header, and per-driver manifests.
- `fs/`
  VFS, FAT32, and memfs.
- `user/programs/`
  C and ELF user programs.
- `user/programs/ushell/`
  C user shell implementation split into smaller include units.
- `user/include/`
  Userland syscall and helper headers.
- `include/`
  Public kernel, driver, filesystem, and architecture headers.
- `archive/legacy-bios/`
  Frozen BIOS/32-bit/FAT12 reference code, excluded from the active build.
- `tools/`
  Image builders and QEMU smoke automation.
- `tools/driver_builder/`
  Host-side unsigned `.drv` builder, signer, and `drvinfo.py` inspection tool.

## Requirements

For the active 64-bit UEFI build:

- `make`
- `nasm`
- `python3`
- `qemu-system-x86_64`
- OVMF firmware files
- `gcc`
- `g++`
- `ld`
- `objcopy`

The archived legacy BIOS tree is not part of the active build requirements.

## Smoke Tests

The active path has QEMU smoke tests:

```sh
make uefi
python3 tools/uefi_smoke.py

make uefi
python3 tools/uefi_userland_smoke.py

make uefi
python3 tools/uefi_screen_smoke.py
```

The smoke scripts use temporary image copies so they can run even when another QEMU instance has the main images open.

## Notes

- `make all64` builds the active FAT32 root image at `bin/os64.bin`.
- `make uefi` builds `bin/uefi_esp.img`.
- `./build.sh` is the active full build helper.
- `./run.sh` is the active QEMU run helper.
- Runtime logs are kept under `logs/`.
- `run hello.drv` is intentionally rejected because `.drv` files are kernel drivers, not user programs.
- Use `drvload hello.drv` for kernel-driver packages. Use `drvunload <name>` and `drvreload <path>` for package lifecycle tests. Use `drvlast` to inspect the last `.drv` loader diagnostic.
- User program results can be inspected with `laststatus`, reaped one at a time with `wait`, or cleared with `reapall`. Each parent keeps the latest three finished child results unreaped; older results are automatically marked reaped. If the process table still fills, the kernel attempts a silent reap before failing a new `run`.
- `uptime` reports PIT Hz, raw ticks, approximate milliseconds, and TSC delta.
- Use `python3 tools/driver_builder/drvinfo.py bin/consumer_c.drv` to inspect a package on the host.
- `build_drv.py` emits unsigned packages; `sign_drv.py` turns them into loadable signed `.drv` files.
- `sign_drv.py --algorithm tpm-local` is a placeholder format path only; the current kernel recognizes it and rejects it as unsupported.
- NX/page permission policy is still being refined.
- Legacy BIOS/32-bit/FAT12 code is frozen and should not be changed as part of active work.

## Clean

```sh
make clean
```

## License

MIT
