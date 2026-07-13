# Phase 3.5 Stabilization Complete

- Date: 2026-07-12
- Tag: `phase-3.5-complete`
- Commit: `27e18e3` (`Close Phase 3.5 stabilization`)
- Repository snapshot: 150 commits, 351 tracked files, 47,906 total lines

Phase 3.5 hardened the kernel and user-service foundation before further
platform expansion. The phase closed with process lifecycle and common wait
behavior stabilized, address-space isolation completed, typed kernel handles
and IPC v2 established, and service supervision permissions enforced.

Concurrency invariants, diagnostic snapshots, fault injection, repeated restart
testing, and soak coverage were added to make failures observable and
repeatable. The ABI and regression expectations were frozen at closure. The
one-hour release gate completed 2,049 health IPC cycles without resource drift.

## Records

- [Starting baseline](../../phases/phase-3.5/baseline.md)
- [Stabilization plan and closure](../../phases/phase-3.5/stabilization.md)
- [ABI freeze](../../phases/phase-3.5/abi_freeze.md)
- [Regression matrix](../../phases/phase-3.5/regression_matrix.md)
- [Fault injection and soak testing](../../testing/fault_injection_and_soak.md)
