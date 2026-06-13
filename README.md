# mini-kernel

Experimental x86 OS project for learning low-level systems work by building a 64-bit kernel, a small C-based userland, a VFS stack, and a UEFI boot path.

The current active path is:

```text
UEFI firmware -> BOOTX64.EFI -> kernel64.bin -> FAT32 root / -> OS64 shell/userland
```

Legacy BIOS/32-bit/FAT12 code still exists for reference, but it is frozen and is not part of current active development.

## Current Status

What works on the active 64-bit UEFI path:

- UEFI bootloader image generation
- UEFI framebuffer handoff
- `BootInfo` handoff with memory map and reserved ranges
- 64-bit long mode kernel entry
- PMM, heap, and runtime paging helpers
- kernel stack supplied by the UEFI loader
- IDT, GDT/TSS, PIT, and keyboard IRQ handling
- syscall path through `int 0x80`
- process table and scheduler prototype
- cooperative `yield`
- timer-based preemption
- sleep / wakeup
- foreground / background job control
- ELF user program loading
- C user programs with `main(void)` and `main(argc, argv)` support
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
- `.drv` package validation and loading
- kernel export/import resolution
- `hello.drv` entry execution through `drvload hello.drv`

## Build

Active 64-bit build:

```sh
make all64
make uefi
```

Equivalent default:

```sh
make all
```

Build artifacts are written under `bin/` and `build/`.

Important active artifacts:

- `bin/kernel64.bin`
- `bin/os64.bin`
  FAT32 root disk image for the active OS path.
- `bin/uefi_esp.img`
  UEFI ESP image containing `BOOTX64.EFI` and `kernel64.bin`.
- `bin/hello.drv`
  Test driver package loaded from the FAT32 root filesystem.

## Run

Run the active UEFI path:

```sh
./run.sh
```

Equivalent helper:

```sh
./run_uefi.sh
```

`run.sh` builds the FAT32 root image and ESP image, refreshes OVMF vars, and starts QEMU with the ESP plus the FAT32 root disk.

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
- `drvcheck [path]`
- `drvload [path]`
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

Built-in drivers currently registered:

- `ata0`
- `fat32`
- `keyboard`
- `pit`

Kernel exports currently available:

- `kernel.klog`
- `kernel.kmalloc`
- `kernel.kfree`

Supported commands:

```text
OS64> drivers
OS64> drvcheck hello.drv
OS64> drvload hello.drv
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
- section loading
- import resolution
- import patching into loaded code
- `driver_entry()` invocation
- driver state transition to `ready`
- `new/delete` and `new[]/delete[]` use in the loader

Not implemented yet:

- real cryptographic signature verification
- relocation application
- driver export-table registration
- driver-to-driver dependency loading
- unload / stop lifecycle
- page-level code/data permissions
- isolation from kernel address space

## Repo Layout

- `src/uefi/`
  UEFI loader, PE/COFF link script, and kernel entry bridge.
- `src/boot/`
  Long mode entry, interrupt stubs, and legacy boot code.
- `src/kernel/`
  64-bit kernel runtime and orchestration.
- `src/kernel/core/`
  Split kernel64 initialization, diagnostics, IRQ, process, and user-mode logic.
- `src/kernel/driver/`
  Driver manager, loader, exports, built-ins, and shell commands.
- `src/arch/x86/`
  PMM, paging, GDT/TSS, and IDT.
- `src/drivers/`
  Terminal, keyboard, PIT, ATA.
- `src/fs/`
  VFS, FAT32, memfs, and frozen legacy FAT12 code.
- `src/user/`
  C and ELF user programs.
- `src/user/ushell/`
  C user shell implementation split into smaller include units.
- `tools/`
  Image builders and QEMU smoke automation.

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

For frozen legacy 32-bit builds:

- `i686-elf-gcc`
- `i686-elf-g++`
- `i686-elf-ld`

## Smoke Tests

The active path has QEMU smoke tests:

```sh
cp /usr/share/OVMF/OVMF_VARS_4M.fd ./bin/OVMF_VARS_4M.fd
python3 tools/uefi_smoke.py

cp /usr/share/OVMF/OVMF_VARS_4M.fd ./bin/OVMF_VARS_4M.fd
python3 tools/uefi_userland_smoke.py

cp /usr/share/OVMF/OVMF_VARS_4M.fd ./bin/OVMF_VARS_4M.fd
python3 tools/uefi_screen_smoke.py
```

The smoke scripts use temporary image copies so they can run even when another QEMU instance has the main images open.

## Notes

- `make all64` builds the active FAT32 root image at `bin/os64.bin`.
- `make uefi` builds `bin/uefi_esp.img`.
- `run hello.drv` is intentionally rejected because `.drv` files are kernel drivers, not user programs.
- Use `drvload hello.drv` for kernel-driver packages.
- NX/page permission policy is still being refined.
- Legacy BIOS/32-bit/FAT12 code is frozen and should not be changed as part of active work.

## Clean

```sh
make clean
```

## License

MIT
