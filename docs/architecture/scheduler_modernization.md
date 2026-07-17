# Scheduler Modernization Plan

This document separates the scheduler work needed to close Phase 4 from the
larger threading and SMP projects that follow it. The order is deliberate:
first make one CPU schedule independently and correctly, then introduce
multiple execution contexts per process, and only then allow several CPUs to
schedule concurrently.

Phase 4H performs the persistent console/GUI display and input handoff in
[Console And GUI Display Handoff](console_gui_handoff.md) before removing the
foreground scheduler helper described here.

## Current Transitional Limitation

The current kernel can save, preempt, wait, wake, and resume one user execution
context per process. Background services and applications nevertheless still
depend on a foreground user program entering the scheduler path while the
kernel shell is active.

The temporary `drive` shell command is an alias for:

```text
run udrive_c.elf
```

`udrive_c.elf` yields repeatedly for 100 timer ticks. This provides a foreground
user context while background processes make progress. It does not initialize
graphics, route input, or present frames. Its presence is evidence of an
execution-model gap, not a supported application-launch mechanism.

Phase 4 must not be reported complete while GUI correctness or service
liveness depends on `drive`, `udrive_c.elf`, or an equivalent foreground
helper.

## Phase 4H: Drive-Free Single-CPU Scheduling

Phase 4H retains one execution context per process and one active CPU. Its
scheduler work is a correctness fix, not the threading or SMP expansion.

Required behavior:

- introduce an explicit kernel idle task or equivalent idle scheduling state;
- let timer preemption select ready user processes while the kernel shell is
  waiting for input;
- allow services and background applications to run without a foreground user
  process anchoring the scheduler;
- keep waiting processes off the ready queue and wake each completed wait at
  most once;
- preserve exact process identity, terminal-state, cleanup, and permission
  invariants from the Phase 3.5 scheduler contract;
- keep the terminal responsive while GUI services and applications execute;
- remove every GUI/input smoke-test call to `drive` or `udrive_c.elf`;
- remove the `drive` shell command and helper program after the drive-free
  regression suite passes.

Required evidence:

1. start the supervised input and window stack, return to an idle kernel shell,
   and observe service health advance without running another user program;
2. launch the restricted GUI application, leave the shell idle, and observe
   focus, redraw, resize, and shutdown events complete from real timer and input
   wakeups;
3. block all runnable user processes and prove that the idle path neither spins
   incorrectly nor corrupts scheduler state;
4. wake timer, IPC, input, and child waits while the shell remains idle and
   prove ready-queue membership and completion occur exactly once;
5. repeat service restart and client churn with no unexplained process, wait,
   handle, mapping, surface, or heap drift.

Phase 4H exits only when the ordinary QEMU GUI and input tests contain no
scheduler-driving helper and the full Phase 4 closure suite passes in that
state.

## Phase 4.5: Threading Foundation

Phase 4.5 separates a schedulable execution context from the process resource
container. It remains valid to bring this up on one CPU first.

### Object Model

- A process owns its address space, handles, mappings, current directory,
  service registrations, permission mask, and process-wide exit state.
- A thread owns its thread id plus generation, saved registers, user stack,
  kernel stack, scheduler state, wait state, priority/accounting data, and
  thread-local storage base.
- Every process starts with one main thread. Additional threads share the
  process address space and process-owned resources.
- Thread exit releases only thread-owned state. Process exit stops and joins
  all remaining threads before process-owned cleanup begins.

### Kernel And SDK Work

- bounded thread table or generation-tagged thread objects;
- `thread_create`, `thread_exit`, `thread_join`, yield, sleep, and identity
  operations;
- independent per-thread kernel and user stacks with guard-page policy;
- wait queues that block threads instead of whole processes;
- mutex, semaphore, condition-variable, and once primitives with explicit
  ownership and cancellation behavior;
- scheduler priority and time-slice accounting with starvation checks;
- diagnostics for process/thread ownership, wait reasons, and runnable state.

### Exit Gate

Multiple threads in one process must execute, block, wake, join, and terminate
without corrupting shared mappings or handles. A failing thread must not leak
its stacks or silently destroy unrelated threads, and process teardown must
remain deterministic under concurrent thread exit.

## Phase 4.6: SMP And Multicore Scheduling

Phase 4.6 permits multiple CPUs to run kernel and user threads concurrently.
It begins only after the single-CPU thread model and synchronization tests are
stable.

### CPU Bring-Up And State

- enumerate and start application processors through the architecture backend;
- add per-CPU current-thread, idle-thread, kernel-stack, interrupt-depth, and
  scheduler data;
- give every online CPU a valid idle path before enabling general scheduling;
- keep architecture-neutral scheduling policy separate from x86_64 APIC and
  startup mechanisms.

### Scheduling And Coordination

- begin with a simple locked global run queue or clearly specified per-CPU run
  queues; optimize only after correctness evidence exists;
- add reschedule IPIs, remote wakeups, CPU affinity, and bounded load balancing;
- implement cross-CPU TLB shootdown before allowing concurrent address-space
  mutation;
- route timer and device interrupts with explicit CPU ownership;
- audit every scheduler, process, VM, handle, IPC, service, VFS, and graphics
  lock for SMP-safe ordering and interrupt-context use;
- define memory-ordering rules for lock-free or atomic state before using them.

### Exit Gate

All online CPUs must run and migrate eligible threads without double-running a
thread, losing a wakeup, resuming a terminal context, or leaking per-CPU state.
Concurrent process exit, mapping changes, IPC, service restart, graphics work,
and TLB invalidation must pass stress and fault tests on at least two virtual
CPUs before SMP becomes the default configuration.

## Required Order

```text
Phase 4H-A
console/GUI display and input handoff
    -> Phase 4H-B
drive-free single-CPU preemption and idle scheduling
    -> Phase 4H-C
lifecycle, fault, resource, and soak closure
    -> Phase 4.5
process/thread separation and synchronization APIs
    -> Phase 4.6
AP startup, per-CPU state, IPIs, TLB shootdown, and SMP scheduling
    -> Phase 5
desktop and applications built on stable background execution
```

Combining these steps into one scheduler rewrite would make failures in idle
execution, thread lifetime, synchronization, and cross-CPU coordination
difficult to isolate. Each phase therefore keeps its own object-model,
resource, fault-injection, and soak exit gate.

## Deferred Optimization

The first correct implementation does not require work stealing, NUMA policy,
real-time scheduling classes, tickless operation, scheduler groups, or lock-free
run queues. Those are later optimizations and must not weaken the lifecycle and
resource invariants established here.
