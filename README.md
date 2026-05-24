# mini-kernel

Experimental x86 kernel and bootloader project focused on learning low-level systems work.

The current default path boots a 64-bit kernel through a BIOS `stage1 -> stage2` loader. A legacy 32-bit path is still kept around for reference.

## Current Status

What works right now:

- BIOS boot sector + second-stage bootloader
- FAT12 boot image generation
- `KERNEL.BIN` lookup from FAT12 root directory
- FAT12 cluster-chain kernel loading
- 64-bit long mode boot
- `BootInfo` handoff
- BIOS E820 memory map handoff
- 64-bit IDT
- page fault / general protection fault / double fault handling
- PIT and keyboard IRQ handling
- FAT12 file commands from the 64-bit shell
- 64-bit PMM
- 64-bit heap
- 64-bit runtime paging helpers
- heap page return back to PMM

Current 64-bit shell commands:

- `help`
- `clear`
- `version`
- `bootinfo`
- `memmap`
- `memstat`
- `echo`
- `write`
- `fill`
- `read`
- `free`
- `dump`
- `atatest`
- `ls`
- `load`
- `save`
- `rm`
- `pagefault`
- `uptime`

## Repo Layout

- `src/boot/`
  Boot code, long mode entry, interrupt stubs
- `src/kernel/`
  Kernel entry and 64-bit shell logic
- `src/arch/x86/`
  IDT, paging, PMM, GDT/TSS-related code
- `src/drivers/`
  Terminal, keyboard, PIT, ATA
- `src/fs/`
  FAT12 implementation
- `tools/`
  Image build helpers

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
./build.sh
```

Equivalent:

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
./run.sh
```

Legacy 32-bit path:

```sh
./run32.sh
```

There is also an explicit 64-bit runner:

```sh
./run64.sh
```

## Notes

- `make all` builds `bin/os64.bin`
- `make all32` builds `bin/os.bin`
- The 64-bit path uses QEMU serial output on stdout
- The 32-bit path still writes debug output to `qemu.log`

## Next Steps

The next major milestone is user mode:

1. 64-bit user-mode entry
2. first syscall path, likely `int 0x80`
3. first user program
4. user shell

After that:

- process/task model
- ELF loader
- syscall-backed file access
- init process
- UEFI boot path

## Clean

```sh
make clean
```

## License

MIT
