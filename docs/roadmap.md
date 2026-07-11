# OS64 Roadmap

Status:

- `[x]` complete
- `[~]` foundation implemented; stabilization or expansion remains
- `[ ]` not implemented

## Current Foundation

- [x] UEFI boot and BootInfo v2 handoff
- [x] Architecture-neutral PMM, VM policy, and kernel heap under `kernel/mm`
- [x] x86_64 paging backend under `arch/x86_64/mm`
- [x] FAT32 root VFS and memfs
- [x] ELF64 user programs, scheduler, and syscall layer
- [x] User SDK v2
  - result codes
  - memory and dynamic file APIs
  - time API
  - 2D graphics API
  - keyboard and common input-event API
  - IPC and service APIs
- [x] Driver Manager v2
  - dependency resolution
  - probe and bind
  - import/export ABI
  - IRQ ABI
  - unload and reload
  - signed DRV packages
- [x] PCI discovery
- [x] GOP framebuffer driver and 2D present pipeline
- [x] Common input-event path
- [x] ACPI S5 shutdown

## Phase 1: Kernel Diagnostics And Hardware Foundation

Detailed tasks and regression coverage:

- `docs/phase1_regression_matrix.md`

Completed:

- [x] Panic subsystem and register dump
- [x] Frame-pointer stack trace foundation
- [x] Double-fault IST stack
- [x] Kernel log ring with levels and subsystem tags
- [x] Diagnostic boot mode
- [x] ACPI RSDP, RSDT/XSDT, and MADT parsing
- [x] Local APIC and IOAPIC with PIC fallback
- [x] User/kernel page-fault and GP-fault separation
- [x] Automated QEMU fault and fallback coverage

## Phase 2: Graphics And Input Foundation

Detailed tasks and regression coverage:

- `docs/phase2_task_breakdown.md`
- `docs/phase2_regression_matrix.md`
- `docs/2d_graphics_library.md`

Completed:

- [x] Reusable 2D surface, clipping, drawing, blit, and text primitives
- [x] Back buffer, dirty rectangles, and partial GOP present
- [x] Stable key, pointer, and common input-event ABI
- [x] Bounded input queues with overflow policy
- [x] Blocking and nonblocking input syscalls
- [x] Focused per-process input delivery
- [x] User-space graphics and event-loop samples

## Phase 3: IPC And User-Space Services

Detailed tasks and regression coverage:

- `docs/phase3_task_breakdown.md`
- `docs/phase3_regression_matrix.md`

Completed:

- [x] Fixed-size IPC message ABI and bounded process mailboxes
- [x] Nonblocking send/receive and blocking receive
- [x] Request/reply helpers, diagnostics, and process-exit cleanup
- [x] Service identity ABI and kernel service registry
- [x] Owner cleanup and stale-pid prevention
- [x] User-space Service Manager v1
- [x] Start, stop, restart, status, and static dependency policy
- [x] `inputd_c.elf` and `displayd_c.elf` placeholder services
- [x] Automated IPC and service integration coverage

Service principles:

- Services are ordinary ELF user programs.
- Drivers remain signed `.drv` packages.
- The kernel supplies bounded transport, ownership, and cleanup mechanisms.
- Service startup and dependency policy stay in `serviced_c.elf`.

## Phase 3.5: Foundation Stabilization

Detailed tasks and Phase 4 entry criteria:

- `docs/phase3_5_stabilization.md`

Planned work:

- [x] Common wait and wakeup core
- [x] Hardened process identity and lifecycle cleanup
- [x] Independent user address spaces and recoverable user faults
- [x] Typed per-process handles, shared-memory objects, and surface handles
- [x] IPC v2 with correlation, timeout, and handle transfer
- [x] Service supervision, permissions, and bounded restart policy
- [ ] Concurrency rules, fault injection, and long-running soak coverage

## Phase 3.6: Driver Packaging And Layout

Detailed tasks and Phase 4 entry criteria:

- `docs/phase3_6_driver_packaging.md`

Planned work:

- [ ] Treat every driver as a package by default
- [ ] Replace permanent `builtin`/`external` directory categories with
  domain-based driver folders
- [ ] Add central `config/drivers.json` policy for artifact, load stage, and
  autoload behavior
- [ ] Move filesystem drivers under `drivers/fs` while keeping VFS as kernel
  infrastructure
- [ ] Generate linked-driver registration and runtime autoload lists from
  central policy
- [ ] Preserve current `.drv` build, autoload, unload/reload, and FAT32 root
  behavior through the migration

## Phase 4: Compositor And Window Server

The next phase must preserve the existing layering:

```text
application
  -> GUI SDK
  -> window protocol over IPC
  -> window service
  -> compositor
  -> display service / graphics driver interface
  -> GOP fallback or hardware graphics driver
```

Planned work:

- [ ] Display-driver abstraction above built-in GOP
- [ ] Shared or transferable graphics surfaces
- [ ] Framebuffer compositor prototype
  - surface ownership
  - z-order
  - damage tracking
  - screen composition
- [ ] Window service
  - create, destroy, move, and resize
  - focus and input routing
  - application IPC protocol
- [ ] GUI application SDK
  - window lifecycle API
  - drawing surface API
  - event loop
  - basic widget foundation

## Phase 5: Desktop Foundation

- [ ] GUI terminal
- [ ] Desktop background
- [ ] Panel and application launcher
- [ ] File manager
- [ ] Settings and shutdown UI
- [ ] Windows-style installed system layout and package conventions

## Parallel Hardware Track

This track may progress beside GUI work, but DMA infrastructure comes first.

- [ ] DMA allocation and mapping API
- [ ] USB xHCI host controller
- [ ] USB enumeration and hub support
- [ ] USB HID keyboard and mouse
- [ ] AHCI or NVMe storage
- [ ] Network driver and network stack
- [ ] Audio subsystem and driver
- [ ] Native GPU driver

## Multi-Architecture Track

- [x] Separate generic memory policy from the x86_64 paging backend
- [ ] Select architecture backends from the build system
- [ ] Move remaining x86_64 CPU and boot assumptions behind architecture APIs
- [ ] Define architecture-neutral interrupt, timer, and context-switch contracts
- [ ] Add an AArch64 UEFI loader and kernel entry
- [ ] Implement AArch64 paging, exception, timer, and context-switch backends

## Engineering Principles

- Keep kernel mechanisms smaller than user-space policy.
- Keep hardware drivers behind the Driver Manager ABI.
- Keep services and GUI components in user space where practical.
- Add one focused regression test for each new responsibility.
- Split modules by responsibility, not by an arbitrary line-count target.
- Stabilize each phase before expanding the next one.
