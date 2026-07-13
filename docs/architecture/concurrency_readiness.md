# Concurrency Readiness

Phase 3.5H makes the current single-CPU kernel explicit about interrupt-safe
shared-state access. It does not add SMP scheduling.

## Spinlock Contract

`KernelSpinlock` saves RFLAGS, disables local interrupts, acquires with atomic
exchange, and restores the caller's original IF state on release. Nested locks
follow these classes:

1. process and scheduler state;
2. address-space mappings;
3. per-process handle tables;
4. IPC mailboxes, input queues, and the service registry;
5. VFS and device state.

Equal/lower-class acquisition, recursive acquisition, and invalid release are
counted as diagnostic violations. `locks` prints acquisition, contention,
maximum-depth, and violation counters.

## Protected State

- scheduler queue, wait deadlines, timer accounting, and scheduling counters
  use the process lock;
- every handle table owns a class-3 lock and supports copy resolution plus
  detach-all cleanup;
- every IPC mailbox and input queue owns a class-4 lock;
- the service registry uses a class-4 global lock;
- stale service pruning copies identity under the registry lock, checks process
  liveness without it, then conditionally removes the unchanged entry;
- handle cleanup detaches entries under lock and destroys referenced objects
  after releasing the table lock.

No protected path sleeps, allocates, copies user memory, performs VFS I/O, or
invokes a driver callback while holding these locks.

## Diagnostic Snapshots

Scheduler/process diagnostics use `SchedulerDiagnosticSnapshot`. It copies
process state and scheduling counters under the class-1 lock, then nests
handle and mailbox snapshot locks in increasing class order. IPC counters in a
snapshot therefore come from one mailbox critical section.

Service diagnostics copy all registered entries in one registry critical
section. Diagnostic reads do not consume queues or mutate delivery counters.

## Tests

`make test-concurrency` covers:

- IF save/restore and nested restoration;
- valid lock ordering;
- order, recursion, and release misuse counters;
- four host threads performing 40,000 contended critical sections;
- handle, process, service, mailbox, and snapshot invariants.

QEMU smoke coverage runs `locks` after ordinary boot and service lifecycle
traffic and requires every misuse counter to remain zero.
