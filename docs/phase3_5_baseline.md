# Phase 3.5 Starting Baseline

This record freezes the tested foundation before Phase 3.5 changes process,
wait, address-space, handle, IPC, or service behavior.

## Revision And Environment

- Date: 2026-07-09
- Starting revision: `bc5db41`
- Host environment: Ubuntu 24.04 under WSL
- QEMU: 8.2.2
- Firmware: OVMF UEFI
- GCC/G++: 13.3.0
- NASM: 2.16.01
- Python: 3.12.3
- Target architecture: x86_64
- Active boot path: UEFI

Only documentation was modified while this baseline was executed. The kernel,
userland, drivers, and test programs correspond to the starting revision.

## Results

| Command | Result | Coverage summary |
| --- | --- | --- |
| `make clean` | PASS | Removed active kernel, userland, driver, and image outputs |
| `make all -j4` | PASS | Clean kernel, SDK, user program, driver, and FAT32 root build |
| `make uefi -j4` | PASS | BOOTX64.EFI and UEFI ESP image generation |
| `make test-phase1` | PASS | Diagnostic boot, faults, ACPI/APIC fallback, memory/NX baseline |
| `python3 tools/uefi_smoke.py` | PASS | Normal UEFI boot and kernel readiness |
| `python3 tools/uefi_userland_smoke.py` | PASS | C shell and userland execution |
| `python3 tools/uefi_screen_smoke.py` | PASS | 1280x800 framebuffer, 35,528 visible pixels |
| `make test-user-sdk` | PASS | 56 SDK, heap, VFS, time, graphics, input, and pointer checks |
| `make test-graphics` | PASS | 2D primitives, surfaces, dirty present, demo, 1280x800 and 800x600 GOP |
| `make test-input` | PASS | Event queues, keyboard translation, focus routing, blocking event loop |
| `make test-ipc` | PASS | Mailbox unit tests, IPC core tests, QEMU request/reply |
| `make test-services` | PASS | Registry, manager lifecycle, dependencies, input/display services |

## Frozen Capacity Limits

These are baseline constraints, not Phase 3.5 target values:

- process table: 8 records
- executable user slots: 4
- one execution context per process
- one shared kernel page-table root with fixed user slots
- scheduler default timeslice: 6 PIT ticks
- PIT frequency: 100 Hz
- bounded per-process input and IPC queues
- static service-manager dependency table

## Known Baseline Limitations

- IPC and input blocking use separate syscall-local check/schedule/halt loops.
- IPC and input wait flags are not first-class scheduler wait reasons.
- Process identity is a reusable raw pid without a generation component.
- User processes do not own independent page-table roots.
- VFS and subsystem handles do not share one typed handle model.
- Large IPC payloads cannot use transferable shared-memory handles.
- Service supervision has no general timeout, health, or restart policy.
- Shared kernel tables have no SMP-capable locking.

These limitations are expected at the starting revision. Phase 3.5 tests must
show that each replacement preserves the passing results above.
