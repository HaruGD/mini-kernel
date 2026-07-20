# Phase 4.5 Implementation Plan

This plan turns the scheduler-modernization direction into an ordered,
testable single-CPU threading project. Each subphase must leave the tree
buildable, preserve all earlier regressions, and satisfy its own exit gate
before the next one is marked complete.

Actual state and evidence belong in [progress.md](progress.md). Contract-to-
test coverage belongs in [regression_matrix.md](regression_matrix.md). Planned
commands in those documents are not evidence until the corresponding Make
targets exist and pass.

## Target Object Model

```text
Process (PID + generation)
  owns address space, image/heap, handles, mappings, cwd, permissions,
       IPC mailbox, input queue, services/VFS state, exit state
  owns 1..N Thread records
          |
          +-- Thread (TID + generation)
                owns saved registers, kernel stack, guarded user stack,
                     scheduler/wait state, accounting, TLS base, exit result

single CPU scheduler -> selects exactly one READY Thread
```

The process is a shared resource container and failure boundary. The thread is
the only schedulable user execution unit. `current_process()` may remain as a
convenience accessor, but it must derive from `current_thread()->process` after
the migration.

Every newly loaded process starts with one main thread. Existing programs must
behave exactly as before while only that main thread exists. Additional
threads share process-owned mappings and handles; they do not receive private
copies.

## Fixed Policies

### Identity And Capacity

- A thread identity is `tid + generation`; a bare TID is diagnostic shorthand,
  never sufficient for ownership or wakeup.
- Reusing a thread slot increments its nonzero generation before publication.
- Thread-table and per-process thread counts are fixed and bounded in the
  first implementation. Exact constants are frozen in 4.5A after memory and
  stack-budget tests, before the public ABI is exposed.
- A thread record is never reused while a join result or scheduler reference
  can still name its old generation.

### Lifecycle

- The loader creates the main thread as part of one rollback-safe process
  construction transaction.
- A created thread is joinable. Exactly one successful join consumes its
  retained exit result; self-join, cross-process join, stale identity, and a
  second consuming join fail deterministically.
- `thread_exit` releases the calling thread's live stack/context state and
  retains only bounded join metadata until joined or process teardown.
- Returning from the process main entry requests process exit. Returning from
  a secondary thread entry is equivalent to `thread_exit` with that return
  value.
- Exiting the final live thread completes the process if process exit is not
  already underway.
- Process exit changes the process to an exiting state once, rejects new
  threads, cancels waits, stops and reaps every remaining thread, then releases
  shared resources exactly once.

Detached threads and joining a thread from another process are not part of the
first ABI. They can be added later without weakening identity or cleanup
rules.

### Scheduling And Waiting

- The ready queue contains thread identities or validated `Thread*` entries,
  never process records.
- Only one thread can be `RUNNING` on the Phase 4.5 single CPU.
- Yield, preemption, sleep, and blocking syscalls save and restore the current
  thread context.
- Mailboxes and input queues stay process-owned. Their wait lists contain
  generation-checked threads, and one queued item wakes at most one waiter.
- Join and condition-variable broadcasts may wake multiple eligible threads,
  but every individual transition remains exactly-once.
- Lock acquisition and blocking are forbidden in interrupt context unless the
  primitive explicitly provides a nonblocking IRQ-safe operation.

### Faults

- Explicit thread exit is local to the calling thread.
- Recoverable syscall errors and thread allocation failures do not kill the
  process or siblings.
- Fatal user page, protection, or control-flow faults terminate the shared
  process and record the faulting thread identity.
- Kernel faults remain kernel faults; the thread abstraction must not disguise
  them as ordinary user exits.

### Synchronization Strategy

The first public synchronization implementation is correctness-first and
blocking. Mutexes, semaphores, and condition variables use bounded kernel
objects/wait queues and generation-checked handles. They do not busy-spin in
Ring 3. A library `once` primitive is built from the same acquire/wait/wake
contract.

Mutexes are non-recursive by default. Unlock by a non-owner fails. Process or
thread termination cancels affected waits and leaves no permanent kernel
owner reference. Priority inheritance, futex-style user-space fast paths,
robust cross-process mutexes, and real-time protocols are deferred.

## 4.5A: Thread Object Model

Goal: introduce the ownership model and bounded thread records without
changing observable one-thread-per-process behavior.

Implementation:

- freeze `ThreadIdentity`, lifecycle states, scheduler states, ownership
  fields, capacity limits, and legal transitions;
- add a bounded generation-tagged thread table and per-process membership;
- make one authoritative lookup path require TID plus generation;
- define the main-thread relationship and process aggregate-state rules;
- centralize allocation, publication, terminal transition, join-result
  retention, and record reuse;
- add assertions for queue membership, ownership, terminal state, and
  generation reuse;
- extend diagnostics to show process/thread relationships without exposing
  kernel pointers.

Exit gate: host tests prove valid transitions, capacity failure, generation
reuse, stale-identity rejection, and idempotent cleanup for isolated thread
records. Existing process behavior is unchanged.

## 4.5B: Main Thread Extraction

Goal: make the initial process execution context a real thread while keeping
all existing binaries and syscalls compatible.

Implementation:

- move saved registers, scheduling state, pause/wait state, time accounting,
  wake state, user stack/guard ownership, and kernel-stack ownership to the
  main `Thread`;
- change the ready queue, timer preemption, resume path, idle selection, and
  current-context accessors to operate on threads;
- derive the owning process from the selected thread for address-space,
  permission, handle, IPC, input, VFS, and service operations;
- keep temporary compatibility accessors read-only and remove them once all
  callers use the correct owner;
- make process load failure roll back both process and main-thread
  construction;
- migrate diagnostics and assertions so no execution field has two writers.

Exit gate: the complete Phase 4 suite passes with exactly one main thread per
process, no scheduler-driving helper, no duplicated context authority, and no
resource drift.

## 4.5C: Thread ABI And SDK

Goal: expose safe creation and lifecycle operations to ordinary user programs.

Implementation:

- freeze versioned public types and syscall numbers for create, current
  identity, exit, join, yield, and sleep;
- accept only a validated user entry point, user argument value, bounded stack
  request, and known flags; never accept a kernel stack or kernel pointer;
- allocate zeroed guarded user-stack pages and a private kernel stack before
  publishing the thread;
- provide a fixed startup trampoline that calls the requested entry and turns
  a return into `thread_exit`;
- return TID plus generation and require the full identity for join;
- define deterministic errors for capacity, invalid entry/stack/flags,
  self-join, stale/cross-process identity, already-consumed result, timeout,
  cancellation, and process exit in progress;
- add matching User SDK declarations, wrappers, and a minimal multithreaded
  test program.

Exit gate: one process creates multiple threads that run, yield, sleep, return
values, join in varying orders, and exhaust/reuse slots without stack, page,
record, or result drift.

## 4.5D: Thread-Aware Waiting

Goal: block only the calling thread for every existing waitable operation.

Implementation:

- migrate timer, child-process, IPC, input, key, and character waits to
  per-thread records;
- add thread-join wait state and generation-checked wait ownership;
- keep IPC and input storage process-wide while maintaining bounded waiter
  sets;
- perform a final nonblocking check before sleeping to close check-to-wait
  races;
- specify one-waiter versus broadcast wake policy for every event source;
- cancel focus-dependent waits, closing-object waits, and process-exit waits
  with deterministic results;
- ensure timeout, signal, cancellation, and termination compete through one
  exactly-once completion transition.

Exit gate: while one thread blocks on each wait class, sibling threads continue
to run. Wake, timeout, cancel, and exit races produce one result and no lost or
duplicate queue entry.

## 4.5E: Synchronization Primitives

Goal: let threads protect shared process state without polling.

Implementation:

- add bounded kernel object types and SDK APIs for mutex, semaphore, and
  condition variable;
- add library-level once initialization with failure and retry semantics;
- enforce mutex owner identity, non-recursive policy, and non-owner unlock
  denial;
- define semaphore maximum/count overflow and exact wake behavior;
- implement condition wait as an atomic release-and-block operation followed
  by mutex reacquisition before return;
- support signal, broadcast, finite timeout, cancellation, and object-close
  races;
- document lock ordering and prohibit blocking acquisition in interrupt
  context;
- close or cancel all object waiters during thread/process teardown without
  stale owner references.

Exit gate: contention, signal/broadcast, timeout, cancellation, owner exit,
object close, and forced allocation failures pass without deadlock, duplicate
wake, count corruption, use-after-free, or resource drift.

## 4.5F: TLS, Accounting, And Fairness

Goal: give every thread isolated runtime state and observable scheduling
behavior.

Implementation:

- add a validated per-thread TLS base and architecture save/restore support;
- provide public set/get or runtime initialization support without exposing
  privileged segment state;
- account runtime, preemption, yield, block, wake, and switch counts per
  thread, with process aggregates derived from thread data;
- apply the documented priority/time-slice policy to runnable threads;
- preserve bounded progress for shell, services, GUI clients, and CPU-bound
  sibling threads on one CPU;
- expose diagnostics for thread identity, owner, state, wait reason, TLS,
  runtime, and queue membership while omitting sensitive addresses where
  appropriate.

Exit gate: TLS values stay isolated across context switches, accounting is
monotonic and attributable, and mixed CPU-bound/blocking workloads satisfy the
documented starvation bound.

## 4.5G: Fault Injection, Soak, And Closure

Goal: certify the complete single-CPU thread model and all earlier contracts.

Implementation and verification:

- inject failures into thread records, user/kernel stack pages, mappings,
  handles, wait nodes, and synchronization-object construction;
- exercise join/exit, timeout/signal, process-exit/thread-exit, object-close,
  and slot-reuse races in deterministic schedules;
- force a fatal user fault in one thread and verify explicit process-wide
  termination, faulting identity diagnostics, and complete cleanup;
- repeatedly create, block, wake, join, and destroy threads while services,
  GUI applications, and the shell continue to make progress;
- run a minimum 60-second thread soak and compare warmed/final resource
  snapshots;
- add an aggregate `make test-phase45` only after every command it invokes
  exists, then run it and the clean full closure suite;
- record exact commands, duration, cycle counts, resource tuples, and
  implementation commits in the progress ledger.

Exit gate: all Phase 4.5 matrix rows and all earlier regression suites pass
from a clean tree; the soak has zero unexplained drift; no terminal/stale
thread is queued or resumed; and the closure evidence is committed.

## Required Order

```text
4.5A object model
  -> 4.5B main-thread extraction
  -> 4.5C public thread ABI
  -> 4.5D per-thread waits
  -> 4.5E synchronization
  -> 4.5F TLS, accounting, fairness
  -> 4.5G fault, soak, closure
  -> Phase 4.6 SMP
```

The implementation must not expose thread creation before the scheduler and
all current execution fields are thread-owned. It must not start additional
CPUs before thread lifetime, waits, synchronization, and teardown are stable
on one CPU.

## Phase Exit Gate

Phase 4.5 is complete only when multiple threads in one process can execute,
block, wake, synchronize, return, join, fault, and terminate with deterministic
ownership and cleanup; the required 60-second soak and clean aggregate suites
pass; and immutable evidence is present in `progress.md`.
