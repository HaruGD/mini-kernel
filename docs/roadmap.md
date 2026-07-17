# OS64 Roadmap

Status:

- `[x]` complete
- `[~]` foundation implemented; stabilization or expansion remains
- `[ ]` not implemented

## Current Foundation

- [x] UEFI boot and BootInfo v3 handoff with bounded verified boot modules
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

- `docs/phases/phase-1/regression_matrix.md`

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

- `docs/phases/phase-2/task_breakdown.md`
- `docs/phases/phase-2/regression_matrix.md`
- `docs/architecture/2d_graphics_library.md`

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

- `docs/phases/phase-3/task_breakdown.md`
- `docs/phases/phase-3/regression_matrix.md`

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
- Every driver remains a package project; product policy selects a kernel-linked
  artifact or signed `.drv` package.
- The kernel supplies bounded transport, ownership, and cleanup mechanisms.
- Service startup and dependency policy stay in `serviced_c.elf`.

## Phase 3.5: Foundation Stabilization

Detailed tasks and Phase 4 entry criteria:

- `docs/phases/phase-3.5/stabilization.md`

Planned work:

- [x] Common wait and wakeup core
- [x] Hardened process identity and lifecycle cleanup
- [x] Independent user address spaces and recoverable user faults
- [x] Typed per-process handles, shared-memory objects, and surface handles
- [x] IPC v2 with correlation, timeout, and handle transfer
- [x] Service supervision, permissions, and bounded restart policy
- [x] Concurrency rules, deterministic fault injection, malformed-request
  coverage, resource accounting, and one-hour soak certification

## Phase 3.6: Driver Packaging And Layout

Detailed tasks and Phase 4 entry criteria:

- `docs/phases/phase-3.6/driver_packaging.md`

Planned work:

- [x] Treat every driver as a package by default
- [x] Replace permanent `builtin`/`external` directory categories with
  domain-based driver folders
- [x] Add central `config/drivers.json` policy for enabled state, artifact,
  load stage, and automatic/manual activation
- [x] Move filesystem drivers under `drivers/fs` while keeping VFS as kernel
  infrastructure
- [x] Generate linked and packaged activation sets for boot, kernel, and
  runtime stages from central policy
- [x] Support verified boot-stage `.drv` handoff through UEFI and BootInfo
- [x] Implement all seven allowed artifact/stage/policy combinations and reject
  every forbidden combination
- [x] Preserve current `.drv` build, autoload, unload/reload, and FAT32 root
  behavior through the migration

Closure and regression coverage:

- `docs/phases/phase-3.6/regression_matrix.md`
- `docs/phases/phase-4/entry_baseline.md`

## Phase 4: Compositor And Window Server

Frozen boundary contract:

- `docs/phases/phase-4/compositor_contracts.md`
- `docs/phases/phase-4/implementation_plan.md`
- `docs/phases/phase-4/progress.md`
- `docs/phases/phase-4/regression_matrix.md`

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

- [x] 4A: Page-backed surface foundation
- [x] 4B: Surface ABI, mapping, and transfer rights
- [x] 4C: Display-service present path and GOP backend abstraction
- [x] 4D: Supervised `windowd` with one full-screen client
- [x] 4E: Bounded multiwindow z-order, damage, and composition
- [x] 4F: `inputd` forwarding and keyboard focus routing
- [x] 4G: Window SDK and first event-driven GUI application
- [ ] 4H: Lifecycle, fault, soak, regression, and closure

Widgets, the desktop shell, per-user session services, alpha composition, and a
separate compositor process are deferred until the Phase 4 lifecycle and
failure gates pass.

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

## Future End Goal: High-Performance Windows Compatibility VM

This is a post-roadmap research goal. It begins only after the native desktop,
preemptive SMP execution, storage, networking, IOMMU, and device-lifecycle
foundations are stable. The target is a general-purpose, low-latency virtual
machine that boots an actual user-supplied and properly licensed Windows
installation rather than reimplementing the Windows API or kernel ABI.

Planned foundation:

- [ ] Define an architecture-neutral hypervisor, vCPU, guest-memory, interrupt,
  and device-model contract
- [ ] Add x86_64 Intel VMX and AMD SVM backends with capability detection and
  safe fallback
- [ ] Execute guest code directly in hardware virtualization mode with EPT/NPT,
  bounded VM-exit handling, and isolated guest memory
- [ ] Keep latency-critical vCPU, memory, interrupt, and IOMMU paths in the
  kernel while placing lifecycle and device policy in a supervised `vmd`
  service
- [ ] Support dedicated physical-core pinning, preallocated guest memory, large
  pages, stable TSC handling, and optional APICv/AVIC acceleration
- [ ] Provide a coherent boot platform with guest UEFI, ACPI, APIC, PCI, timers,
  storage, display, keyboard, and pointer devices
- [ ] Boot Windows from a user-provided installation image and virtual disk,
  then reach a stable desktop inside one OS64 window
- [ ] Add IOMMU interrupt remapping and exclusive GPU, NVMe, USB, and other PCI
  device passthrough without allowing concurrent host ownership
- [ ] Run compatible Windows applications, services, and kernel drivers against
  either supported virtual devices or explicitly passed-through hardware
- [ ] Integrate guest display, audio, input, clipboard, files, and networking
  with the native OS64 service boundaries
- [ ] Add an optional Windows guest agent that maps individual guest application
  windows onto OS64 `windowd` surfaces for seamless desktop integration
- [ ] Add bounded failure recovery, resource accounting, snapshots, suspend and
  resume, deterministic fault injection, and long-duration VM soak coverage

Scope and distribution boundaries:

- OS64 remains visibly and honestly virtualized; anti-cheat, DRM, licensing, or
  virtual-machine detection circumvention is not a project feature.
- OS64 does not distribute Windows images, product keys, proprietary firmware,
  game files, or third-party kernel drivers.
- Users must supply valid licenses and follow the terms of Windows, applications,
  games, online services, and hardware vendors. Some services may reject virtual
  machines or impose account restrictions even when the VM operates correctly.
- The first supported profile may intentionally freeze one Windows x64 release,
  one CPU family, and one virtual hardware layout before broader compatibility.

## Engineering Principles

- Keep kernel mechanisms smaller than user-space policy.
- Keep hardware drivers behind the Driver Manager ABI.
- Keep services and GUI components in user space where practical.
- Add one focused regression test for each new responsibility.
- Split modules by responsibility, not by an arbitrary line-count target.
- Stabilize each phase before expanding the next one.
