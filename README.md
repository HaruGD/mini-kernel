# Bootloader

Experimental x86 bootloader and kernel project.

This project is early-stage and unstable. It is intended for learning and low-level systems development experiments.

## Current Scope

- 16-bit boot sector
- 32-bit kernel entry
- IDT setup
- Physical memory manager
- Heap
- Paging
- Terminal output
- Keyboard, PIT, and ATA driver experiments
- FAT12 and shell experiments

## Requirements

- `make`
- `nasm`
- `qemu-system-i386`
- `i686-elf-gcc`
- `i686-elf-g++`
- `i686-elf-ld`

## Build

```sh
./build.sh
```

The build creates files under `bin/` and `build/`.

## Run

```sh
./run.sh
```

QEMU writes debug output to `qemu.log`.

## Clean

```sh
make clean
```

## Status

This is not a production operating system. Interfaces, memory layout, drivers, and boot behavior may change at any time.

## License

MIT
