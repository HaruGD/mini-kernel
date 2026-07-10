# Phase 3.6 Driver Packaging And Layout

Phase 3.6 is a structural cleanup phase between foundation stabilization and
the compositor/window-server work. The goal is to make every driver look like a
driver package first, regardless of whether it is later linked into the kernel
or loaded as a `.drv` file.

This phase should happen after Phase 3.5 invariants are stable and before
Phase 4 starts depending on display, input, filesystem, and service boundaries.

## Design Principles

- Every driver is a package.
- Directory layout should group drivers by domain, not by load mechanism.
- `builtin` and `external` should not be permanent top-level driver categories.
- Whether a driver is kernel-linked or emitted as `.drv` is a build policy.
- Whether a driver is prepared during boot, kernel initialization, or runtime
  autoload is a load-stage policy.
- Filesystem implementations such as FAT32 are filesystem drivers.
- VFS routing remains kernel infrastructure, not a filesystem driver.
- Keep one clear source of truth for driver build and load policy.

## Target Layout

Long-term target:

```text
drivers/
  block/
    ata/
    ahci/
    nvme/

  bus/
    pci/
    usb_xhci/

  display/
    gop/
    bochs_vga/
    amdgpu/

  fs/
    fat32/
    memfs/

  input/
    ps2_keyboard/
    usb_hid_keyboard/

  timer/
    pit/
    hpet/

  demo/
    hello_c/
    hello_cpp/
    gop_demo_c/

config/
  drivers.json
```

Each driver directory should eventually use a common shape:

```text
drivers/<domain>/<driver-name>/
  driver.json
  Makefile
  src/
  include/
```

`src/` and `include/` may be omitted for tiny drivers during migration, but
new nontrivial drivers should use them from the start.

## Central Driver Policy

The central driver configuration should describe the whole driver set using
paths relative to `drivers/`:

```json
{
  "drivers": [
    {
      "name": "fat32",
      "path": "fs/fat32",
      "artifact": "drv",
      "load_stage": "kernel",
      "autoload": true
    },
    {
      "name": "gop",
      "path": "display/gop",
      "artifact": "linked",
      "load_stage": "boot",
      "autoload": true
    },
    {
      "name": "hello_c",
      "path": "demo/hello_c",
      "artifact": "drv",
      "load_stage": "runtime",
      "autoload": false
    }
  ]
}
```

The per-driver `driver.json` remains the package manifest. The central
`config/drivers.json` is the product/build policy: which drivers are included,
how they are built, and when they are loaded.

## Policy Fields

`artifact` describes the build result:

- `drv`: build a signed `.drv` package.
- `linked`: compile and link the driver into `kernel64.bin`.

The default for ordinary drivers should be `drv`. A driver should become
`linked` only when early boot, recovery, or current kernel limitations require
it.

`load_stage` describes when the driver must be prepared:

- `boot`: needed before or during the earliest kernel handoff path.
- `kernel`: needed during kernel initialization before normal userland starts.
- `runtime`: loaded after the OS is up, either automatically or manually.

`autoload` describes whether the driver is automatically loaded at its stage:

- `true`: load automatically at the selected stage.
- `false`: build or ship it, but require an explicit load command or policy.

These fields replace permanent `builtin` vs `external` directory categories.

## Migration Plan

### 3.6A. Policy Model

- [ ] **D01: Define `config/drivers.json` schema**
  Add a documented schema for `name`, `path`, `artifact`, `load_stage`, and
  `autoload`.
  Completion: invalid paths, unknown artifacts, and unknown stages are rejected
  by a host-side validation tool.

- [ ] **D02: Add driver policy validator**
  Validate central policy and per-driver manifests without building the kernel.
  Completion: `make test-driver-policy` catches missing driver directories and
  malformed policy entries.

### 3.6B. Unified Driver Project Shape

- [ ] **D03: Move external sample drivers into domain folders**
  Move samples from `drivers/external/*` to `drivers/demo/*` without changing
  produced `.drv` names.
  Completion: all existing sample `.drv` packages still build and autoload
  behavior is unchanged.

- [ ] **D04: Move built-in hardware drivers into domain folders**
  Move ATA, GOP, keyboard, PIT, and terminal into domain paths such as
  `drivers/block/ata` and `drivers/display/gop`.
  Completion: generated built-in registry still produces the same driver
  records.

- [ ] **D05: Move filesystem drivers under `drivers/fs`**
  Move FAT32 implementation from `fs/fat32` to `drivers/fs/fat32` and keep VFS
  infrastructure outside the driver tree.
  Completion: FAT32 root mount, file I/O, and directory tests keep passing.

### 3.6C. Build Integration

- [ ] **D06: Generate driver build lists from central policy**
  Stop discovering driver projects by `drivers/builtin` and
  `drivers/external`. Use `config/drivers.json` instead.
  Completion: the same root image contents are produced from policy-driven
  build lists.

- [ ] **D07: Generate linked-driver registry from central policy**
  Replace built-in manifest globbing with policy entries where
  `artifact=linked`.
  Completion: switching a driver between `drv` and `linked` changes build
  behavior without moving its directory.

- [ ] **D08: Generate runtime autoload set from central policy**
  Use policy entries where `artifact=drv`, `load_stage=runtime`, and
  `autoload=true` to decide which packages are included for boot autoload.
  Completion: manual-only drivers remain available but do not autoload.

### 3.6D. Documentation And Regression

- [ ] **D09: Update driver ABI and README references**
  Document that all drivers are packages and that build/load behavior is
  policy-driven.
  Completion: docs no longer describe `builtin` and `external` as long-term
  categories.

- [ ] **D10: Add migration regression matrix**
  Record build, boot, driver list, autoload, manual load, and FAT32 root
  coverage.
  Completion: Phase 4 can begin with driver layout churn closed.

## Phase 4 Entry Criteria

Before compositor and window-server work begins:

- [ ] `config/drivers.json` exists and validates.
- [ ] Existing sample `.drv` packages build from the new layout.
- [ ] Linked driver registration is generated from policy.
- [ ] FAT32 root still mounts and supports existing file operations.
- [ ] `drivers` shell output remains behaviorally equivalent.
- [ ] Runtime autoload still loads the same enabled `.drv` packages.
- [ ] Manual `drvload`, `drvunload`, and `drvreload` still work.
- [ ] README and Driver ABI docs describe the new model.

