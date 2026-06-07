# mini-kernel

Experimental x86 OS project for learning low-level systems work by building a BIOS boot chain, a 64-bit kernel, and a small C-based userland.

The current default path is the 64-bit system:

```text
BIOS -> stage1 -> stage2 -> kernel64 -> USHELL_C.ELF
```

A legacy 32-bit path still exists for reference, but active development is focused on the 64-bit side.

## Current Status

What works right now:

- BIOS boot sector + second-stage loader
- FAT12 boot image generation
- 64-bit long mode kernel boot
- `BootInfo` handoff
- BIOS E820 memory map handoff
- IDT / PIT / keyboard IRQ handling
- PMM + heap + runtime paging helpers
- user mode entry
- syscall path (`int 0x80`)
- process table and scheduler prototype
- cooperative `yield`
- timer-based preemption
- sleep / wakeup
- foreground / background job control
- ELF user program loading
- C user programs with `main(void)` and `main(argc, argv)` support
- default C shell userland (`USHELL_C.ELF`)
- VFS layer
- root FAT12 backend mounted at `/`
- memory-backed filesystem mounted at `/mem`
- read-only FAT32 backend mounted at `/fat32`
- handle-based file I/O:
  - `open`
  - `read`
  - `write`
  - `close`
  - `seek`
  - `tell`

## Shells

There are two shell layers:

### Kernel shell (`OS64>`)

This is the BIOS/kernel-side shell used before entering userland.

Typical commands include:

- `help`
- `clear`
- `version`
- `bootinfo`
- `memmap`
- `memstat`
- `dump`
- `sched`
- `mounts`
- `ls [path]`
- `load [file]`
- `save [file]`
- `rm [file]`
- `run [file]`
- `resume`
- `usertest`
- `ushell`
- `ushellc`
- `uptime`

### User shell (`USHELL_C.ELF`)

`ushell` now boots into the C user shell by default.

Main built-ins:

- `help`, `?`, `about`, `exit`, `clear`, `cls`
- `version`, `uptime`, `jobs`, `ps`, `wait`, `laststatus`, `reapall`
- `pwd`, `cd [path]`
- `ls [path]`, `cat [file]`, `touch [file]`, `save [file] [text]`, `rm [file]`
- `mkdir [path]`, `rmdir [path]`
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

Shell shortcuts that run standalone tools:

- `sched`
- `memstat`
- `bootinfo`
- `mounts`

## Standalone User Programs

The project now leans heavily on C/ELF user programs.

Examples:

- `UHELLO_C.ELF`
- `UINFO_C.ELF`
- `USLEEP_C.ELF`
- `UYIELD_C.ELF`
- `UBUSY_C.ELF`
- `UFAULT_C.ELF`
- `UARGS_C.ELF`
- `UIO_C.ELF`

Standalone file/status tools:

- `ULS_C.ELF`
- `UMKDIR_C.ELF`
- `URMDIR_C.ELF`
- `UTOUCH_C.ELF`
- `USAVE_C.ELF`
- `UCAT_C.ELF`
- `URM_C.ELF`
- `UPID_C.ELF`
- `USCHD_C.ELF`
- `UMEM_C.ELF`
- `UVERS_C.ELF`
- `UBOOT_C.ELF`
- `UMNTS_C.ELF`

Examples from the user shell:

```text
csh> cd /mem/docs
csh> ucat test.txt
csh> usave test.txt hello world
csh> uio /mem/handle.txt hello via handle
csh> run uargs_c.elf alpha beta gamma
```

## Filesystems

Current VFS mounts:

- `/` -> FAT12 backend
- `/mem` -> memfs backend
- `/fat32` -> FAT32 backend

Important note:

- `memfs` now supports nested directories such as `/mem/docs/readme.txt`
- `pwd`, `cd`, relative paths, and `.` / `..` path normalization work in `USHELL_C`
- most shell built-ins and tool aliases resolve relative paths against the current working directory
- FAT12 is still treated as a simpler root-oriented backend; the richer directory behavior currently lives in `memfs`
- FAT32 is currently read-only and 8.3-oriented, but it already supports:
  - root directory listing
  - subdirectory listing
  - file info lookup
  - file reads such as `/fat32/HELLO.TXT` and `/fat32/DOCS/README.TXT`

## Repo Layout

- `src/boot/`
  Boot code, long mode entry, interrupt stubs
- `src/kernel/`
  64-bit kernel runtime and orchestration
- `src/arch/x86/`
  PMM, paging, GDT/TSS, IDT
- `src/drivers/`
  Terminal, keyboard, PIT, ATA
- `src/fs/`
  FAT12, VFS, memfs backend logic
- `src/user/`
  C and ELF user programs
- `tools/`
  Build helpers and QEMU automation

## Kernel Structure

The large 64-bit kernel file has been split into focused modules:

- `src/kernel/kernel64.cpp`
  Boot/initialization and high-level orchestration
- `src/kernel/kutil64.cpp`
  Kernel utility and print helpers
- `src/kernel/kernel_diag.cpp`
  Process/scheduler/VFS diagnostic output
- `src/kernel/process64.cpp`
  Process table and scheduler state handling
- `src/kernel/userprog64.cpp`
  ELF/user program loader helpers and argv stack preparation
- `src/kernel/syscall64.cpp`
  Syscall dispatch
- `src/kernel/ksh64.cpp`
  Kernel shell command handling

## Requirements

For the current default 64-bit build:

- `make`
- `nasm`
- `python3`
- `qemu-system-x86_64`
- `gcc`
- `g++`
- `ld`
- `objcopy`

For the legacy 32-bit path:

- `i686-elf-gcc`
- `i686-elf-g++`
- `i686-elf-ld`

`build.sh` also expects your cross-toolchain path to be available at:

```sh
/home/home/opt/cross/bin
```

## Build

Default 64-bit build:

```sh
make all64
```

Equivalent aliases:

```sh
make all
```

Legacy 32-bit build:

```sh
make all32
```

Build artifacts are written under `bin/` and `build/`.

## Run

Default 64-bit path:

```sh
./run64.sh
```

Equivalent:

```sh
./run.sh
```

Legacy 32-bit path:

```sh
./run32.sh
```

## Quick Test Flow

From the kernel shell:

```text
OS64> ushell
```

From the C user shell:

```text
csh> mkdir /mem/docs
csh> cd /mem/docs
csh> pwd
/mem/docs
csh> touch note.txt
csh> save note.txt hello cwd
csh> cat note.txt
hello cwd
csh> uls .
note.txt
csh> umkdir sub
csh> cd sub
csh> usave nested.txt hello nested
csh> ucat nested.txt
hello nested
csh> cd ..
csh> rm note.txt
csh> urmdir sub
```

Handle-based I/O test:

```text
csh> uio /mem/handle.txt hello via handle
csh> ucat /mem/handle.txt
```

FAT32 smoke test:

```text
csh> mounts
csh> uls /fat32
HELLO.TXT
DOCS/
csh> ucat /fat32/HELLO.TXT
hello fat32
csh> uls /fat32/DOCS
README.TXT
csh> ucat /fat32/DOCS/README.TXT
fat32 nested
```

Argument passing test:

```text
csh> run uargs_c.elf alpha beta gamma
```

## Notes

- `make all64` builds `bin/os64.bin`
- the 64-bit path uses QEMU serial output on stdout
- the system defaults to the C user shell (`USHELL_C.ELF`)
- C user programs are the main userland path now
- handle-based VFS I/O is available
- `memfs` has directory support plus shell-level `cwd`/`cd`
- FAT32 is mounted as a second IDE disk image when `bin/fat32.img` exists
- FAT32 is currently read-only and intended as the next disk-style filesystem path after FAT12
- richer directory semantics for writable non-memfs backends and fuller FD behavior are still in progress
- NX is currently relaxed for stability while the protection model is being refined

## Next Steps

The current priorities are:

1. extend FAT32 beyond read-only 8.3 access
2. continue generalizing directory APIs beyond `memfs`
3. continue strengthening VFS/file-handle behavior
4. decide how broadly process-level `cwd` should propagate through userland
5. revisit memory protection / NX
6. eventually explore a UEFI boot path

## Clean

```sh
make clean
```

## License

MIT
