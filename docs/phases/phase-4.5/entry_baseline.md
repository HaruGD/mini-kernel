# Phase 4.5 Entry Baseline

This document records the state from which the threading foundation starts.
It is a baseline, not a claim that the Phase 4.5 object model already exists.

## Entry Gate

Phase 4 closed on 2026-07-20.

- implementation commits: `31b681e` and `8d44929`;
- closure documentation commit: `064e835`;
- annotated milestone tag: `phase-4-complete`, pointing to `8d44929`;
- `make test-phase4`: PASS in 216.5 seconds;
- 60-second GUI soak: 35 application cycles, 140 window lifecycles, and 7
  GUI service-stack restarts;
- warmed/final GUI-soak resource tuple:
  `(4,21,1,0,4,0,1,27173,1944432,1961984)` with zero drift;
- clean full `make test-closure`: PASS in 651.30 seconds, including User SDK
  91/91 and the independent 42-cycle service soak.

The optional one-hour release soak is not an entry requirement for Phase 4.5.

## Current Execution Model

The kernel is single-CPU and has one schedulable user execution context per
`Process`. The process record currently combines two responsibilities:

1. process-wide resource ownership: address space, heap/image mappings,
   surface mappings, handle table, input queue, IPC mailbox, current directory,
   permissions, parent identity, and exit result;
2. execution-context ownership: saved registers, user stack and guard,
   scheduler state, time slice and runtime accounting, pause reason, wait
   reason/deadline/result, and wake tick.

`PROCESS_TABLE_SIZE` and `SCHED_QUEUE_SIZE` are both 8. The scheduler queue
contains `Process*`, and `current_process()` is also the current execution
context. There is no independent TID namespace, per-thread stack ownership,
join state, TLS base, or first-class kernel thread object.

The existing saved register fields and user-stack metadata in `Process` are
authoritative. Phase 4.5 must move that authority once; it must not leave a
second independently mutable copy in both `Process` and `Thread`.

## Inherited Invariants

The rules in
[Process And Scheduler Invariants](../../architecture/process_scheduler_invariants.md)
remain binding during migration:

- identity comparisons use a numeric id plus a nonzero generation;
- terminal execution is never selectable or queued;
- a waiting execution context is never selected as ready;
- one execution context has at most one active wait reason;
- wake and queue insertion occur at most once;
- terminal cleanup is idempotent;
- process-table reuse changes the process generation;
- handles, mappings, IPC state, services, VFS state, and other process-owned
  resources are released exactly once.

Phase 4.5 extends these rules to threads. It does not weaken the process,
handle, IPC, service, VFS, input, surface, or GUI lifecycle contracts already
closed by Phase 4.

## Existing Wait And Wake Behavior

A blocked operation currently pauses the whole process through the common wait
fields in `Process`. Timer, child, IPC, input, key, and character waits share
that state. Enqueue paths wake the one available process execution context.

After process/thread separation:

- the wait record and saved syscall result belong to the calling thread;
- IPC mailbox and input queue storage remain process-owned;
- an arriving item wakes at most one eligible waiter unless an operation is
  explicitly specified as a broadcast;
- process termination cancels every remaining thread wait before process-owned
  resources are destroyed;
- no wake path may resurrect a terminal or generation-mismatched thread.

## Constraints On The Migration

- Keep the system bootable and all Phase 4 tests passing after every
  subphase.
- Preserve the existing one-main-thread behavior before exposing
  `thread_create` to applications.
- Use fixed, checked limits before considering dynamically growing tables.
- Allocate independent guarded user and kernel stacks for every runnable
  thread; never reuse another live thread's stack.
- Do not schedule two threads simultaneously. SMP begins only in Phase 4.6.
- Do not hold interrupts disabled across allocation, user copying, IPC work,
  or an unbounded scan.
- Do not add a second scheduler-driving helper or restore the removed `drive`
  dependency.
- Keep GUI services and ordinary applications operational while the shell is
  idle.

## Fault Policy Baseline

An explicit `thread_exit` ends only the calling thread. Allocation failure or
a rejected thread syscall must return an error without terminating sibling
threads.

A synchronous user fault that indicates a corrupted shared address space,
such as an invalid page access or general-protection fault, terminates the
whole process by default. The diagnostic record must identify the faulting
TID plus generation and the process-wide reason. This is an explicit shared-
address-space safety policy, not silent sibling destruction.

When any thread requests process exit, the kernel prevents new thread
creation, cancels and stops the other threads, releases all thread-owned state,
and only then performs process-owned cleanup once.

## Entry Decision

All required entry evidence is present. Phase 4.5A may begin. Phase 4.6 and
Phase 5 remain blocked on the complete Phase 4.5 exit gate.
