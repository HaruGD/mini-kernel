# Phase 3.5 Regression Matrix

This matrix is the Phase 3.5 closure record. `make test-closure` runs the full
automated quick/qualification suite after a build. Release validation starts
with `make clean`, `make all`, and `make uefi`.

## Certified Results

- Date: 2026-07-12
- Clean kernel/SDK/userland/driver build: PASS
- UEFI normal and diagnostic images: PASS
- `make test-closure`: PASS in 425 seconds
- SDK integration: 73 passed, 0 failed
- Screen smoke: 1280x800, 35,528 visible pixels
- Qualification soak: 60 seconds, 38 cycles, zero resource drift
- Release soak: 3,600 seconds, 2,049 cycles, zero resource drift
- Lock order, recursion, and release violations: zero

| Invariant | Automated evidence |
| --- | --- |
| User faults terminate only the offending process | `make test-phase1` |
| Kernel faults still panic and ACPI/APIC fallback remains bounded | `make test-phase1` |
| Process identity generations reject stale records | `make test-process-lifecycle` |
| Process launch/exit/fault/reap returns slots to reuse | `make test-process-lifecycle` |
| Per-process address spaces, heap growth, and user copies remain isolated | `make test-user-sdk`, `make test-phase1` |
| Typed handles reject stale, wrong-type, and insufficient-right tokens | `make test-kernel-handles` |
| Shared memory/surfaces unwind and refcount correctly | `make test-kernel-handles`, `make test-graphics` |
| IPC v1/v2 validation, correlation, timeout, transfer, and 100,000-message stress | `make test-ipc` |
| Input queues and blocking event delivery preserve ownership | `make test-input` |
| Service registration and stale-owner cleanup remain bounded | `make test-services` |
| Service start/stop/restart/crash/permission policy remains bounded | `make test-services` |
| IRQ/process shared state has no lock-order misuse | `make test-concurrency`, `python3 tools/uefi_smoke.py` |
| PMM through shared-object failure injection unwinds cleanly | `make test-fault-injection` |
| Malformed syscall/IPC requests fail without collateral termination | `make test-user-sdk`, `make test-fault-injection` |
| Resource accounting returns to its warmed baseline | `make test-soak` |
| One-hour service/input/display/IPC churn has no panic, hang, lock violation, or drift | `make test-soak-hour` |
| Kernel and SDK agree on all frozen shared ABI layouts | `make test-abi-freeze` |
| Boot, userland, framebuffer, shutdown, driver, and filesystem paths remain operational | `make test-uefi-smoke`, `make test-uefi-userland`, `make test-uefi-screen`, `make test-shutdown` |

## Closure Commands

```sh
make clean
make all
make uefi
make test-closure
```

The one-hour gate is intentionally separate so ordinary closure reruns remain
practical:

```sh
make test-soak-hour
```

## Residual Risks

- The kernel is single-CPU; SMP scheduling and CPU-local lock tracking remain
  out of scope.
- The one-hour gate covers one deterministic workload and QEMU q35 device
  model, not arbitrary hardware timing or every allocation interleaving.
- Driver IRQ hooks are contract-checked but malicious third-party driver code
  can still violate nonblocking rules.
- Address-space page-table roots are deliberately cached per process slot;
  soak baselines are recorded after warming every slot.
- Shared-object accounting is coherent for the current single-CPU syscall
  model; a future SMP object manager needs its own object-table lock.
