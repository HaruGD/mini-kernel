# Phase 4.5: Threading Foundation

Phase 4.5 separates a schedulable thread from the process that owns an address
space and resources. The first implementation remains single-CPU. Its purpose
is to make multithreaded programs correct before Phase 4.6 permits multiple
CPUs to execute kernel and user code concurrently.

## Documents

- [Entry baseline](entry_baseline.md): the exact Phase 4 exit state and the
  process/scheduler limitations inherited by this phase.
- [Implementation plan](implementation_plan.md): subphases 4.5A through 4.5G,
  architectural decisions, ordering, and exit gates.
- [Regression matrix](regression_matrix.md): required contract coverage and
  reserved future test-target names.
- [Progress ledger](progress.md): live status and immutable evidence records.
- [Scheduler modernization plan](../../architecture/scheduler_modernization.md):
  the larger Phase 4H -> 4.5 -> 4.6 scheduling sequence.

## Current Status

4.5A, 4.5B, and 4.5C completed on 2026-07-20 in implementation commit
`63cb234`. The kernel now has bounded generation-tagged thread records, a
thread-selecting single-CPU scheduler, an extracted main thread for every
process, private user/kernel stacks, and User SDK 2.2 lifecycle APIs. The
focused aggregate and the complete Phase 4 suite pass with zero warmed/final
resource drift.

The next implementation task is 4.5D. Existing wait state is now stored on a
thread, but 4.5D must certify multiple same-process waiters, queue-specific
wake policy, and timeout/signal/cancel races before thread-aware waiting is
called complete.

## Scope Boundary

Phase 4.5 includes single-CPU user threads, per-thread waits and stacks,
correctness-first synchronization, thread-local storage, diagnostics, fault
handling, and stress closure.

It does not include application-processor startup, simultaneous execution on
multiple CPUs, work stealing, real-time scheduling classes, a desktop shell,
or a separate compositor process. Those remain Phase 4.6 or later work.
