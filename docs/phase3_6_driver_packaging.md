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
- Whether a driver is activated during boot, kernel initialization, or runtime
  is a load-stage policy, while automatic vs manual activation is a separate
  load-policy choice.
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
  settings.json
  Makefile
  src/
  include/
```

`src/` and `include/` may be omitted for tiny drivers during migration, but
new nontrivial drivers should use them from the start.

Every driver owns both files:

- `settings.json` describes the driver itself and travels with the driver;
- `Makefile` describes how that driver project compiles as linked objects or a
  `.drv` package.

The driver-local files must not decide whether a product enables the driver,
links it into the kernel, ships it as `.drv`, or activates it. Those choices
belong only to `config/drivers.json`.

## Driver-Local Settings

`settings.json` is the package identity and capability manifest. A typical
driver-local file looks like this:

```json
{
  "schema_version": 1,
  "name": "hello_c",
  "display_name": "Hello C sample driver",
  "version": "0.1.0",
  "description": "Minimal C driver package used by regression tests.",
  "domain": "demo",
  "entry": "driver_entry",
  "permissions": [],
  "dependencies": [],
  "exports": []
}
```

Driver-local fields describe intrinsic properties:

- `schema_version`: settings schema version;
- `name`: stable driver identity, unique in the product policy;
- `display_name`: human-readable name;
- `version`: package version;
- `description`: short purpose summary;
- `domain`: `block`, `bus`, `display`, `fs`, `input`, `timer`, or `demo`;
- `entry`: runtime `.drv` entry symbol when supported;
- `permissions`: permissions the driver implementation may request;
- `dependencies`: other driver identities required by the implementation;
- `exports`: symbols exported by the driver package.

A driver that supports kernel linking may also carry a `linked` integration
block with implementation facts such as required headers, extern declarations,
and instance symbol. The block describes *how* it can be linked; it does not
select `artifact=linked`.

```json
{
  "linked": {
    "instance": "&ata",
    "includes": ["drivers/ata.h"],
    "externs": ["extern ATADriver ata;"]
  }
}
```

The local `Makefile` is required even for small drivers. During migration it
must expose a common interface for source discovery, linked-object builds, and
`.drv` package builds. Unsupported artifact modes must fail clearly rather
than silently producing the wrong artifact.

## Central Driver Policy

The central driver configuration should describe the whole driver set using
paths relative to `drivers/`:

```json
{
  "drivers": [
    {
      "name": "fat32",
      "path": "fs/fat32",
      "enabled": true,
      "artifact": "drv",
      "load_stage": "kernel",
      "load_policy": "automatic"
    },
    {
      "name": "gop",
      "path": "display/gop",
      "enabled": true,
      "artifact": "linked",
      "load_stage": "boot",
      "load_policy": "automatic"
    },
    {
      "name": "hello_c",
      "path": "demo/hello_c",
      "enabled": true,
      "artifact": "drv",
      "load_stage": "runtime",
      "load_policy": "manual"
    }
  ]
}
```

The per-driver `settings.json` is the package manifest. The central
`config/drivers.json` is the product/build policy: which drivers are enabled,
how they are built, and when they are loaded.

## Policy Fields

`enabled` is the product-level build switch:

- `true`: build the selected artifact and include it in the product as policy
  requires;
- `false`: do not compile, link, package, ship, or activate the driver.

An enabled policy entry must reference a directory containing both
`settings.json` and `Makefile`, and its `name` must exactly match the local
settings identity.

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

`load_policy` describes who activates the driver at its stage:

- `automatic`: the stage owner activates it automatically.
- `manual`: build and ship it, but require an explicit runtime load command.

`load_stage` always describes activation time rather than merely when the
artifact becomes available. Linked code may be present in the kernel image but
remain inactive until `kernel` or `runtime`. A boot-stage `.drv` is loaded by
UEFI and handed to the kernel as a verified boot module.

## Supported Policy Combinations

Phase 3.6 implements every non-forbidden combination below:

| Artifact | Stage | Policy | Required behavior |
| --- | --- | --- | --- |
| `linked` | `boot` | `automatic` | Present in the kernel image and activated during early boot |
| `linked` | `kernel` | `automatic` | Present in the kernel image and activated during kernel initialization |
| `linked` | `runtime` | `automatic` | Present in the kernel image and automatically activated after runtime begins |
| `drv` | `boot` | `automatic` | UEFI loads and verifies the package, then hands it to the kernel for early activation |
| `drv` | `kernel` | `automatic` | Kernel loads it after driver loader/storage readiness and before userland |
| `drv` | `runtime` | `automatic` | Runtime driver manager loads it automatically |
| `drv` | `runtime` | `manual` | Package is shipped and activated only through an explicit command |

Forbidden combinations are rejected by the host validator:

- any `boot + manual` or `kernel + manual` entry, because no runtime manual
  control surface exists at those stages;
- any `linked + manual` entry, including runtime, because linked drivers use
  policy-controlled activation rather than `drvload`;
- a driver depending on another driver from a later stage;
- an automatic driver depending on a manual driver.

Stage dependency order is `boot < kernel < runtime`. Dependencies may point to
the same or an earlier stage only. A disabled entry keeps fully valid settings
so it can be enabled with one policy change, but it produces no artifact and is
never activated.

These fields replace permanent `builtin` vs `external` directory categories.

## Migration Plan

### 3.6A. Policy Model

- [ ] **D01: Define driver settings and central policy schemas**
  Add documented schemas for per-driver `settings.json` and central
  `config/drivers.json`. Central entries define `name`, `path`, `enabled`,
  `artifact`, `load_stage`, and `load_policy`.
  Completion: invalid paths, missing local files, name mismatches, unknown
  artifacts/stages/policies, forbidden combinations, and invalid dependency
  direction are rejected by a host-side validation tool.

- [ ] **D02: Add driver policy validator**
  Validate central policy, driver-local settings, and the required local
  `Makefile` without building the kernel.
  Completion: `make test-driver-policy` catches missing driver directories and
  malformed or contradictory settings/policy entries.

### 3.6B. Unified Driver Project Shape

- [ ] **D03: Move external sample drivers into domain folders**
  Move samples from `drivers/external/*` to `drivers/demo/*` without changing
  produced `.drv` names, and rename each local `driver.json` to `settings.json`.
  Completion: all existing sample `.drv` packages still build and autoload
  behavior is unchanged.

- [ ] **D04: Move built-in hardware drivers into domain folders**
  Move ATA, GOP, keyboard, PIT, and terminal into domain paths such as
  `drivers/block/ata` and `drivers/display/gop`. Each project retains its own
  `Makefile` and migrates its manifest to `settings.json`.
  Completion: generated built-in registry still produces the same driver
  records.

- [ ] **D05: Move filesystem drivers under `drivers/fs`**
  Move FAT32 implementation from `fs/fat32` to `drivers/fs/fat32` and keep VFS
  infrastructure outside the driver tree. FAT32 also owns a local
  `settings.json` and `Makefile`.
  Completion: FAT32 root mount, file I/O, and directory tests keep passing.

### 3.6C. Build Integration

- [ ] **D06: Generate driver build lists from central policy**
  Stop discovering driver projects by `drivers/builtin` and
  `drivers/external`. Use enabled entries in `config/drivers.json`, then invoke
  the referenced driver-local Makefile interface.
  Completion: the same root image contents are produced from policy-driven
  build lists.

- [ ] **D07: Generate linked-driver registry from central policy**
  Replace built-in manifest globbing with policy entries where
  `enabled=true` and `artifact=linked`, combining central selection with the
  local settings `linked` integration data.
  Generate separate boot, kernel, and runtime activation sets for linked
  drivers.
  Completion: switching a driver between `drv` and `linked` or changing its
  stage changes build/activation behavior without moving its directory.

- [ ] **D08: Generate packaged-driver activation sets from central policy**
  Generate automatic boot, kernel, and runtime `.drv` sets plus the shipped
  runtime-manual set from enabled policy entries.
  Completion: every allowed `.drv` combination enters exactly one generated
  set, while manual-only drivers remain available but never autoload.

- [ ] **D09: Add boot `.drv` module handoff**
  Teach UEFI to load and verify boot-stage `.drv` packages, describe them in a
  bounded BootInfo module list, and let the early kernel driver loader resolve
  and activate them.
  Completion: a boot-stage package activates before kernel-stage drivers, and
  malformed, unsigned, oversized, or dependency-invalid modules are rejected
  without corrupting boot state.

- [ ] **D10: Add staged kernel and runtime activation**
  Execute linked and packaged activation plans at their exact stages. Runtime
  automatic activation happens once after normal runtime begins; runtime
  manual packages remain inactive until `drvload`.
  Completion: all seven supported combinations have focused automated tests,
  and every forbidden combination is rejected by policy validation.

### 3.6D. Documentation And Regression

- [ ] **D11: Update driver ABI and README references**
  Document that all drivers are packages and that build/load behavior is
  policy-driven. Document the separate responsibilities of driver-local
  `settings.json`, driver-local `Makefile`, and central `config/drivers.json`.
  Completion: docs no longer describe `builtin` and `external` as long-term
  categories.

- [ ] **D12: Add migration regression matrix**
  Record build, boot-module handoff, all staged activation sets, driver list,
  automatic/manual load, forbidden policy validation, and FAT32 root coverage.
  Completion: Phase 4 can begin with driver layout churn closed.

## Phase 4 Entry Criteria

Before compositor and window-server work begins:

- [ ] `config/drivers.json` exists and validates.
- [ ] Every enabled driver directory contains a valid `settings.json` and
  `Makefile`.
- [ ] Existing sample `.drv` packages build from the new layout.
- [ ] All seven allowed artifact/stage/policy combinations pass focused tests.
- [ ] Every forbidden combination is rejected by the policy validator.
- [ ] Linked driver registration and staged activation are generated from
  policy.
- [ ] Boot-stage `.drv` packages are verified and handed off through BootInfo.
- [ ] FAT32 root still mounts and supports existing file operations.
- [ ] `drivers` shell output remains behaviorally equivalent.
- [ ] Runtime automatic loading still loads the same enabled `.drv` packages.
- [ ] Manual `drvload`, `drvunload`, and `drvreload` still work.
- [ ] README and Driver ABI docs describe the new model.
