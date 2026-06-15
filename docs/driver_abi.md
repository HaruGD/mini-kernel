# Driver ABI

This document describes the active `.drv` ABI for the UEFI kernel path.
It reflects the current implementation, not a future target design.

## Package Layout

Current `.drv` files contain these blocks in order:

1. `DrvHeader`
2. `DrvManifest`
3. section table
4. symbol table
5. import table
6. export table
7. relocation table
8. signature block
9. certificate block
10. section payloads

The kernel validates the header, table shapes, section ranges, signature, and
certificate before loading anything into memory.

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
- `dependency_count` must be `0` for now.
- `permissions` must only use known permission bits.

## Permissions

Supported permission bits:

- `PCI`
- `MMIO`
- `INTERRUPT`
- `BLOCK`
- `VFS`
- `INPUT`
- `TIMER`

Permissions are checked both when resolving imports and when resolving exports.
If the manifest does not grant the required permission, the load is rejected.

## Symbols and Imports

The builder packages ELF relocatable objects into driver symbols and imports.

Current naming rules:

- `driver_entry` is the required entry symbol.
- `driver_exit` is optional and is called during unload when present.
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
6. resolve imports
7. resolve the entry symbol
8. register exports
9. mark it `linked`
10. call `driver_entry()`
11. mark it `ready`

On failure, the loader:

- rolls back module exports
- frees loaded section memory
- leaves the record as `failed` or `rejected`

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
- `kernel.pci_read_config32`
- `kernel.pci_write_config32`
- `kernel.pci_device_count`
- `kernel.pci_get_device`
- `kernel.pci_find_device`
- `kernel.pci_get_bar`
- `kernel.pci_enable_memory_space`
- `kernel.pci_enable_bus_mastering`
- `kernel.mmio_read32`
- `kernel.mmio_write32`
- `kernel.vfs_open`
- `kernel.vfs_read`
- `kernel.vfs_write`
- `kernel.vfs_close`
- `kernel.block_read_sector`
- `kernel.block_write_sector`

These are the current ABI surface for hardware-oriented drivers.

The active helper header `os64_driver.h` exposes the same PCI discovery APIs
for C driver projects, using the `kernel__<name>` import naming convention.

## Notes

- The loader currently expects driver packages built by the in-tree builder.
- Dependency tables are not enabled yet.
- The ABI is intentionally small so new hardware drivers can land without
  dragging policy into the driver binary format too early.
