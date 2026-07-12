# Phase 4 Entry Baseline

This record freezes the foundation immediately before compositor and window
service implementation. The baseline parent is commit `5107211`, which closes
Phase 3.6. The preflight working set adds live boot-driver coverage, exact
linked readiness timing, display ownership IRQ bounds, and the Phase 4
contracts referenced below.

## Qualification Result

- Date: 2026-07-13
- `make test-closure`: PASS in 458.6 seconds after all preflight changes
- Driver policy/layout/build matrix: PASS, all seven allowed combinations
- Driver migration matrix: PASS, R01-R12
- Live boot-driver QEMU matrix: PASS for signed activation, dependency order,
  unsigned/tampered/dependency rejection, and count/size bounds
- User SDK integration: 73 passed, 0 failed
- UEFI normal, diagnostic, userland, screen, and shutdown tests: PASS
- Screen smoke: 1280x800, 30,372 visible pixels
- Graphics, input, IPC, service, concurrency, and fault-injection groups: PASS
- 60-second service soak: 37 cycles, no reported drift
- Worktree before preflight changes: clean

The one-hour soak is deliberately not rerun for this preflight. Its last Phase
3.5 certification remains historical evidence rather than a new Phase 4 entry
claim.

## Frozen Foundations

- UEFI-only active boot path with BootInfo v3.
- FAT32 ramdisk root and memfs mount.
- Independent process address spaces and recoverable user faults.
- Typed process handles and refcounted shared-memory/surface objects.
- IPC v2 correlation, filtering, timeout, and handle transfer.
- Supervised user-space services with permissions and bounded restart.
- Domain-organized driver packages and central build/activation policy.
- Kernel 2D surfaces, clipping, blit, text, dirty tracking, and GOP present.
- Common keyboard/pointer event ABI and focused process delivery.

## Preflight Gates

- [x] Phase 3.5 and Phase 3.6 closure criteria pass.
- [x] Full closure passes after Phase 3.6.
- [x] Roadmap reflects BootInfo v3 and Phase 3.6 completion.
- [x] Linked driver records become ready only after hardware initialization.
- [x] Display ownership keeps IRQ-off sections bounded to state transitions.
- [x] Live boot-stage `.drv` success and rejection matrix passes.
- [x] Phase 4 surface, display, window, and input contracts are frozen.

The final two boxes are completed by the same preflight change set. Phase 4
feature implementation starts only after every box is checked.
