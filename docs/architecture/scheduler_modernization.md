# Scheduler Modernization Plan

This document separates the scheduler work needed to close Phase 4 from the
larger threading and SMP projects that follow it. The order is deliberate:
first make one CPU schedule independently and correctly, then introduce
multiple execution contexts per process, and only then allow several CPUs to
schedule concurrently.

Phase 4H performs the persistent console/GUI display and input handoff in
[Console And GUI Display Handoff](console_gui_handoff.md) and removes the
foreground scheduler helper described here. Phase 4H-A, 4H-B, and 4H-C were
completed and certified on 2026-07-20.

## Implemented Phase 4H-B State

The kernel still has one user execution context per process and one active CPU,
but background progress no longer depends on a foreground helper. The kernel
shell is the top-level idle context: it drains console input, selects ready
processes, publishes a deferred prompt only after foreground completion, and
then executes `sti; hlt`.

The former temporary command was:

```text
run udrive_c.elf
```

`drive` and `udrive_c.elf` have now been removed from the shell, user-program
image, and GUI/input tests. Timer, IPC, input, child, and idle wakeups drive
normal progress directly.

The implementation also prevents resumed background children from blocking
their parent service manager with a new child wait, and recovers scheduler
readiness from the process table if the bounded queue misses a ready record.
These changes close the helper dependency. The Phase 4H-C fault, recovery,
60-second soak, aggregate regression, and full closure gates also pass.

### Historical Foreground IPC Stall

The same execution-model gap affects ordinary service control. On 2026-07-17,
the following sequence reproduced a deterministic stall after `windowd` and
its `displayd` dependency had started:

```text
OS64> service start window
[usvcctl] start window OK ...
Returned from user program ...
OS64> service
Running user program: usvcctl_c.elf [pid=0x00000005 parent=0x00000000]
OS64>
```

The second command is a service-manager ping. PID 5 sends an IPC request and
enters a blocking receive, but no reply or terminal process record is printed.
The kernel shell prompt is exposed prematurely while foreground input ownership
still belongs to the waiting client, so the prompt cannot accept commands.
This was not a `windowd` deadlock or QEMU halt. It was an invalid
foreground-wait/shell-return transition compounded by background children
re-arming `PROCESS_WAIT_CHILD` on `serviced`. The 4H-B implementation now waits
for the exact foreground PID plus generation, defers the prompt, and applies
child waits only to foreground children. Repeated bare `service` and
`service status window` QEMU regression now pass without a helper.

The plural `services` kernel command remains the service-registry diagnostic.
The singular bare `service` command intentionally pings the service manager and
must also complete safely.

## Phase 4H: Drive-Free Single-CPU Scheduling

Phase 4H retains one execution context per process and one active CPU. Its
scheduler work is a correctness fix, not the threading or SMP expansion.

The behavior below is implemented and certified by the Phase 4 aggregate and
full closure suites.

Required behavior:

- introduce an explicit kernel idle task or equivalent idle scheduling state;
- let timer preemption select ready user processes while the kernel shell is
  waiting for input;
- allow services and background applications to run without a foreground user
  process anchoring the scheduler;
- keep waiting processes off the ready queue and wake each completed wait at
  most once;
- when a foreground process blocks on IPC, run the ready service manager,
  deliver its reply, resume the exact waiting PID plus generation, and restore
  the kernel shell only after that foreground process exits or fails;
- never print or accept a kernel shell prompt while foreground input ownership
  still belongs to a live waiting user process;
- give `usvcctl_c.elf` a bounded reply timeout so a missing or failed service
  manager returns a diagnostic instead of creating an infinite foreground
  wait;
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
5. after `service start window`, run bare `service`, `service status window`,
   and repeated service-control requests; require a reply or bounded error,
   one terminal process result, restored shell input, and no `drive` helper;
6. repeat service restart and client churn with no unexplained process, wait,
   handle, mapping, surface, or heap drift.

Phase 4H exits only when the ordinary QEMU GUI and input tests contain no
scheduler-driving helper and the full Phase 4 closure suite passes in that
state.

## Phase 4.5: Threading Foundation

Phase 4.5 separates a schedulable execution context from the process resource
container. It remains valid to bring this up on one CPU first.

Phase 4.5 completed on 2026-07-25. The object model, main-thread extraction,
thread-selecting scheduler, private stacks, public thread lifecycle ABI,
per-thread waits, synchronization objects, TLS, accounting, single-CPU
fairness, deterministic fault injection, fatal-fault cleanup, and 60-second
churn certification are implemented. The clean Phase 4.5 aggregate and full
project closure suites pass; Phase 4.6 is now the active scheduler milestone.

The detailed entry baseline, subphase order, fixed lifecycle policies,
regression matrix, and live evidence ledger are indexed in
[Phase 4.5: Threading Foundation](../phases/phase-4.5/README.md).

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

The entry baseline, fixed policies, ordered 4.6A through 4.6H implementation
plan, regression matrix, and live evidence ledger are indexed in
[Phase 4.6: SMP And Multicore Scheduling](../phases/phase-4.6/README.md).
The first scheduler remains a locked global queue; per-CPU queues and work
stealing are explicitly deferred until that model is measured and certified.
The same plan reserves kernel GS for validated CPU-local state, gives
NMI/Double Fault independent emergency IST recovery, requires each CPU to
self-calibrate its Local APIC timer, standardizes irqsave/preemption spinlock
ownership, and places every TLB acknowledgement wait after ordinary locks have
been released.

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
