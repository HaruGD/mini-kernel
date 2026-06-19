# Driver ABI

This document describes the active `.drv` ABI for the UEFI kernel path.
It reflects the current implementation, not a future target design.

## Package Layout

Current `.drv` files contain these blocks in order:

1. `DrvHeader`
2. `DrvManifest`
3. optional dependency table
4. section table
5. symbol table
6. import table
7. export table
8. relocation table
9. signature block
10. certificate block
11. section payloads

The kernel validates the header, table shapes, section ranges, signature, and
certificate before loading anything into memory.

Current hard requirements:

- `format_version` must be `1`.
- `abi_version` must be `1`.
- `arch` must be `x86_64` (`0x8664`).
- table entry sizes must be at least the ABI struct sizes.
- signature and certificate blocks must be present.

## Language Policy

The OS64 driver boundary is a C ABI.

All lifecycle and exported entry points that the kernel resolves must use
unmangled C symbols:

```c
os64_u64 driver_entry(void);
os64_u64 driver_exit(void);
os64_u64 driver_probe_pci(const os64_pci_device_info* device);
```

C++ drivers must expose those entry points with `extern "C"`:

```cpp
extern "C" os64_u64 driver_entry();
```

Driver implementation code may be written in C or in restricted freestanding
C++. The C ABI remains the contract between the kernel, the builder, and the
loaded package.

Supported C++ style:

- classes, structs, methods, constructors for explicitly created objects
- `constexpr`, inline helpers, and scoped enums
- small templates when they do not pull in runtime support
- explicit C wrappers around lifecycle, probe, import, and export symbols

Not supported for drivers:

- exceptions
- RTTI and `dynamic_cast`
- the C++ standard library
- iostreams, `std::string`, `std::vector`, allocators, or locale support
- global/static constructor dependencies
- thread-safe static local initialization

Large hardware drivers may use restricted C++ internally, but `.drv` lifecycle
symbols, kernel imports, and driver exports must remain C ABI symbols.

## Manifest

The manifest currently uses these fields:

- `name`
- `version`
- `entry_symbol`
- `permissions`
- `boot_modes`
- `flags`
- `dependency_count`

Current rules:

- `name` and `entry_symbol` must be present.
- `boot_modes` must include `NORMAL`.
- `boot_modes` may only use `NORMAL`, `SAFE`, and `RECOVERY` bits.
- `dependency_count` may be `0..8`.
- `permissions` must only use known permission bits.

When `dependency_count` is nonzero, dependency entries immediately follow
`DrvManifest` inside the manifest block. Each dependency names another driver
record that must already be present and at least `ready` by default. The
autoload pass retries packages, so dependency order inside the FAT32 root does
not need to be perfect.

Current dependency entry fields:

- `name`
- `flags`
- `min_state`

`flags` currently supports `DRV_DEP_REQUIRED`. `min_state=0` means `ready`.

## Permissions

Supported permission bits:

- `PCI`
- `MMIO`
- `INTERRUPT`
- `BLOCK`
- `VFS`
- `INPUT`
- `TIMER`
- `DISPLAY`

Permissions are checked both when resolving imports and when resolving exports.
If the manifest does not grant the required permission, the load is rejected.

## Symbols and Imports

The builder packages ELF relocatable objects into driver symbols and imports.

Current naming rules:

- `driver_entry` is the required entry symbol.
- `driver_exit` is optional and is called during unload when present.
- `driver_probe_pci` is optional and is called once for each unbound PCI device
  when the driver has `PCI` permission.
- An unresolved ELF symbol named `kernel__klog` maps to import `kernel.klog`.
- An unresolved ELF symbol named `provider_c__provider_ping` maps to
  import `provider_c.provider_ping`.

The active C driver helper header, `os64_driver.h`, exposes kernel imports as
function pointers with the `kernel__<name>` naming convention.

## Sections

Supported section kinds:

- `CODE`
- `RODATA`
- `DATA`
- `BSS`

The loader allocates memory for each section, zero-fills it, copies section
contents, and then applies relocations.

Current section rules:

- section names must be null-terminated.
- section kind must be one of the supported section kinds.
- alignment must be a nonzero power of two and no larger than one page.
- `memory_size` must be greater than or equal to `file_size`.
- BSS sections must not carry file payload bytes.

After relocation/import patching, loaded package memory is protected as:

- `CODE`: executable, read-only
- `RODATA`: read-only, NX
- `DATA`/`BSS`: writable, NX

Driver sections are mapped into a dedicated driver virtual address range instead
of being left in the kernel heap.

## Validation

The kernel rejects malformed packages before loading sections.

Current checks include:

- header magic, file size, format version, ABI version, and architecture
- manifest string shape, permissions, boot modes, and dependency count
- section table ranges, kinds, alignment, and file payload ranges
- symbol kinds and symbol ranges inside their target sections
- import names, required permissions, patch section index, and patch offset
- export names, export kinds, and required permission bits
- relocation type, section index, symbol index, and patch width
- local test signature and certificate consistency

## Signatures

The current kernel accepts the local test signature path only.

Supported format slots are present for:

- `LOCAL_TEST`
- `ROOT_KEY`
- `TPM_LOCAL`

Current behavior:

- `LOCAL_TEST` packages load successfully when the checksum and certificate
  fields match.
- `ROOT_KEY` and `TPM_LOCAL` are reserved format slots and are rejected by the
  current kernel.

## Load Lifecycle

The current sequence is:

1. validate header and tables
2. validate manifest policy
3. register the driver record
4. mark it `loading`
5. allocate and fill sections
6. resolve declared dependencies
7. resolve imports
8. resolve lifecycle symbols
9. register exports
10. mark it `linked`
11. call `driver_entry()`
12. mark it `ready`
13. call optional PCI probe for unbound PCI devices

On failure, the loader:

- rolls back module exports
- unregisters IRQ hooks and PCI bindings during unload paths
- frees loaded section memory
- leaves the record as `failed` or `rejected`

## PCI Probe And Bind

Drivers that request `PCI` permission may expose:

```c
os64_u64 driver_probe_pci(const os64_pci_device_info* device);
```

The driver manager calls this after `driver_entry()` has succeeded. The probe
may call `os64_pci_bind_device(device, flags)` to claim the device. The kernel
keeps a PCI binding registry so later diagnostics can show which driver owns
which PCI function.

Current rules:

- only drivers with `PCI` permission may bind PCI devices.
- one PCI function can be bound to one driver at a time.
- bindings are removed automatically on driver unload.
- `pci_probe_c.drv` is the current smoke driver for this path.

## IRQ Hooks

Drivers that request `INTERRUPT` permission may import:

- `kernel.irq_register`
- `kernel.irq_unregister`

The active helper header exposes these as:

```c
os64_irq_register(irq, handler);
os64_irq_unregister(irq, handler);
```

Current IRQ ABI rules:

- IRQ numbers are legacy PIC IRQ lines `0..15`.
- hooks are passive observers called from the existing kernel IRQ path.
- IRQ0 is dispatched from the PIT timer handler.
- IRQ1 is dispatched from the keyboard handler.
- hooks are removed automatically on driver unload.
- `irq_timer_c.drv` is the current smoke driver for this path.

## Unload And Reload

Unload is supported through the kernel shell and loader API.

Rules:

- built-in drivers cannot be unloaded
- a module with ready dependents cannot be unloaded
- `driver_exit()` is called first when it exists
- exports are removed on unload
- the driver record is removed after unload succeeds

Reload is implemented as unload followed by load of the same package image.

## Kernel Exports

The current kernel export set is:

- `kernel.klog`
- `kernel.kmalloc`
- `kernel.kfree`
- `kernel.gop_get_info`
- `kernel.gop_clear`
- `kernel.gop_putpixel`
- `kernel.gop_fill_rect`
- `kernel.pci_read_config32`
- `kernel.pci_write_config32`
- `kernel.pci_device_count`
- `kernel.pci_get_device`
- `kernel.pci_find_device`
- `kernel.pci_get_bar`
- `kernel.pci_map_bar`
- `kernel.pci_enable_memory_space`
- `kernel.pci_enable_bus_mastering`
- `kernel.pci_bind_device`
- `kernel.irq_register`
- `kernel.irq_unregister`
- `kernel.mmio_read32`
- `kernel.mmio_write32`
- `kernel.vfs_open`
- `kernel.vfs_read`
- `kernel.vfs_write`
- `kernel.vfs_close`
- `kernel.block_read_sector`
- `kernel.block_write_sector`

These are the current ABI surface for hardware-oriented drivers.

`kernel.pci_map_bar` returns a kernel virtual address for an MMIO BAR and
requires both `PCI` and `MMIO` permission bits.

`kernel.gop_*` functions expose the active UEFI GOP framebuffer as a display
service for GUI-style drivers.

`gop_demo_c.drv` is the current minimal display-permission ABI smoke driver. It
imports `kernel.gop_get_info`, `kernel.gop_fill_rect`, and
`kernel.gop_putpixel` with the `DISPLAY` permission.

The active helper header `os64_driver.h` exposes the same PCI discovery APIs
for C driver projects, using the `kernel__<name>` import naming convention.

## Notes

- The loader currently expects driver packages built by the in-tree builder.
- The ABI is intentionally small so new hardware drivers can land without
  dragging policy into the driver binary format too early.
