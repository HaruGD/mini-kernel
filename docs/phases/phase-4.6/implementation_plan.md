# Phase 4.6 Implementation Plan

This plan turns the Phase 4.5 single-CPU threading system into a bounded,
testable SMP system. Each subphase must leave one-CPU boot and all earlier
regressions working. An AP is not useful merely because it reached a C
function: it becomes an online scheduler CPU only after every state and
ownership gate below passes.

Actual state and immutable evidence belong in [progress.md](progress.md).
Contract-to-test coverage belongs in
[regression_matrix.md](regression_matrix.md). Reserved target names are plans,
not executed evidence.

## Target Execution Model

```text
ACPI MADT
  -> bounded CPU topology (logical id <-> APIC id)
       |
       +-- CpuLocal[0] BSP
       |     owns current thread, idle context, entry/interrupt state,
       |          preemption state, GDT/TSS/IST, lock stack, counters
       |
       +-- CpuLocal[1..N] APs
             own the same CPU-local classes of state

all online CPUs
  -> one locked global ready queue
       -> selects READY Thread
       -> atomically assigns thread.running_cpu
       -> runs at most once on one CPU

address space
  -> active CPU mask + TLB generation
       -> shootdown request/IPI/acknowledgement
       -> page release only after every required acknowledgement
```

The process remains the shared resource and failure boundary. The thread
remains the schedulable user execution unit. A logical CPU is a bounded kernel
execution resource, not a new ownership boundary for process resources.

## Fixed Policies

### CPU Identity And Capacity

- The first implementation supports at most eight logical CPUs.
- Logical CPU 0 is always the BSP. Logical IDs are dense internal indices and
  never substitutes for firmware or APIC IDs.
- A CPU record retains its ACPI processor identity, APIC identity, BSP flag,
  supported/enabled flags, lifecycle state, and diagnostic counters.
- CPU lifecycle is:
  `DISCOVERED -> PREPARED -> STARTING -> ONLINE -> OFFLINE/FAILED`.
- Duplicate APIC IDs, malformed MADT entries, unsupported IDs, and topology
  overflow are reported and never silently truncated into runnable CPUs.
- The first startup backend uses xAPIC IDs and rejects CPUs that require
  x2APIC mode. x2APIC support is deferred, not emulated by truncation.
- CPU hotplug and re-online after failure are outside the first lifecycle.

### CPU-Local State

Every online CPU has one authoritative `CpuLocal` record containing:

- logical CPU and APIC identity;
- current thread and idle-context pointers;
- kernel entry nesting, interrupt depth, and preemption-disable count;
- CPU-local scheduler and timer counters;
- held-spinlock stack and lock-depth diagnostics;
- GDT/TSS state, Ring 0 stack, and exception IST ownership;
- pending reschedule and TLB-shootdown state.

The BSP migrates to the same `CpuLocal` access path before any AP is started.
The first x86_64 SMP implementation reserves `IA32_GS_BASE` permanently for
the kernel `CpuLocal` pointer. User TLS remains FS-based, CR4.FSGSBASE remains
disabled, no public operation may change GS, and ordinary user/kernel entry
does not use `SWAPGS`. This makes GS state unambiguous even when an NMI arrives
between arbitrary instructions. A future user-GS feature requires a separate
paranoid entry protocol and is not an implicit extension of this phase.

Every `CpuLocal` begins with a fixed validation header containing a magic,
logical CPU ID, APIC ID, and self pointer. NMI and Double Fault use distinct
per-CPU IST stacks allocated from static aligned, fixed-stride arrays. Their
assembly entry derives a candidate CPU index from the emergency-stack range
and validates the header, self pointer, and APIC identity before using normal
CPU-local state. A mismatch enters a minimal emergency panic record and halt
path; it never continues through the ordinary scheduler or fault path.

NMI and Double Fault handlers never acquire ordinary spinlocks, allocate,
copy user memory, schedule, or wait for another CPU. The Double Fault IST is
usable before the CPU publishes `ONLINE`. The initial NMI/Double Fault path
therefore does not rely solely on a valid interrupted GS base. NMI entry sets
a CPU-local emergency-active guard before ordinary diagnostics; unexpected
reentry is a non-returning emergency failure, and the handler never executes
an instruction that deliberately unblocks NMI before `iretq`. Double Fault is
always non-returning.

### AP Startup And Failure

- The BSP allocates and validates every AP startup stack and CPU-local record
  before sending startup IPIs.
- The x86_64 trampoline is below 1 MiB, identity reachable during startup, and
  has a bounded mailbox containing only the selected AP's parameters.
- Startup uses the architecturally ordered INIT and SIPI sequence with a
  bounded timeout and an acquire/release online handshake.
- An AP installs its CPU-local base, CR3, GDT, TSS, IST, IDT, Local APIC, and
  idle stack before publishing `ONLINE`.
- An AP does not enter the global scheduler until 4.6D explicitly releases
  scheduler participation.
- A missing AP leaves a named `FAILED` record and the system continues with
  the remaining online CPUs. Closure tests that request two or four QEMU CPUs
  still require all requested test CPUs to come online.

### Scheduling

- The first SMP scheduler uses one global ready queue protected by one
  scheduler lock.
- `READY -> RUNNING` removes the thread from the queue and assigns exactly one
  `running_cpu`. `RUNNING -> READY/WAITING/TERMINAL` clears that assignment
  under the same scheduler authority.
- An idle context is CPU-owned kernel state and is not published as an
  ordinary user thread or joinable process thread.
- A running thread never migrates. Migration occurs only after it becomes
  ready and before another CPU claims it.
- Each CPU makes scheduling decisions locally after a timer interrupt,
  explicit yield, blocking transition, exit, or reschedule IPI.
- Priority quanta remain 4/6/8 ticks initially. SMP must not change the public
  priority semantics closed by Phase 4.5.
- Affinity begins as a bounded CPU mask. An empty, offline-only, malformed, or
  out-of-range mask is rejected. Removing the current CPU from a running
  thread takes effect at the next safe scheduling boundary.

### Timekeeping, Interrupts, And IPIs

- PIT/IRQ0 remains the single owner of global monotonic time and timeout
  advancement until a later time-source project replaces it.
- Per-CPU Local APIC timers account runtime and expire the local scheduling
  quantum. They do not independently increment global wall-clock ticks.
- The BSP detects invariant TSC and CPUID leaves `0x15`/`0x16` and publishes a
  common frequency reference. If that information is unavailable, the
  BSP-owned PIT epoch is the fallback reference.
- Each CPU measures its own Local APIC decrement rate against that common
  reference. The BSP does not pretend to read a remote CPU's Local APIC timer.
- Every CPU records calibration source, measured frequency, reload value,
  sample window, and error. A CPU outside the frozen tolerance remains online
  for diagnostics but is not released into scheduler participation.
- Timer regression compares one-, two-, and four-vCPU global time and
  per-CPU quantum duration so CPU count cannot multiply wall time or silently
  skew scheduling.
- Keyboard and other external device interrupts remain routed to the BSP
  until 4.6G assigns a tested owner.
- Reschedule and TLB shootdown use separate fixed IPI vectors with distinct
  pending state and counters.
- IPI handlers are bounded, allocation-free, nonblocking, and acknowledge the
  Local APIC exactly once.
- Remote wake sets all shared state with release ordering before sending the
  IPI; the target observes it with acquire ordering.

### Memory Ordering And TLB Safety

- Cross-CPU publication uses explicit relaxed/acquire/release/acq_rel/seq_cst
  choices. `volatile` alone is never treated as synchronization.
- Lock acquisition/release remains the default publication mechanism for
  complex shared state.
- Every address space records a TLB generation and the CPUs on which it may
  currently have cached translations.
- Every CPU records its loaded address-space identity and observed TLB
  generation. Context switch compares that generation and performs a local
  flush before user execution when it is stale.
- A mapping mutation publishes invalidation work before sending shootdown
  IPIs. Required targets acknowledge the matching address-space identity and
  generation.
- One address space has at most one serialized shootdown transaction. Its
  operation token prevents a released/reacquired VM lock from accepting an
  acknowledgement for an older mutation.
- Mapping removal first clears the PTE under the address-space/VM lock and
  moves physical pages, page-table pages, virtual ranges, and address-space
  identities to a quarantine list. None can be freed or reused yet.
- After publishing the request and target mask, the initiator releases every
  ordinary subsystem spinlock. It then sends IPIs and waits with local
  preemption disabled but maskable interrupts enabled. Entry to this path
  asserts that the caller arrived from an interrupt-enabled thread context.
- The initiating CPU performs its own required invalidation synchronously.
  The remaining target mask is captured under scheduler/address-space
  coordination; a CPU that begins running the address space afterward must
  observe the new generation and flush before entering user mode.
- The IPI handler reads only its bounded CPU-local mailbox, performs `invlpg`
  or a full local address-space flush, publishes the matching acknowledgement,
  and sends EOI. It takes no VM/heap/process lock and performs no allocation.
- Only after every required acknowledgement may the initiator reacquire the
  VM lock, revalidate the operation token, and retire quarantined state.
- A timeout never counts as acknowledgement. Unless the target is already
  proven offline by a separate CPU-failure protocol, pages remain quarantined
  and the first correctness implementation enters a controlled fatal
  diagnostic rather than risk stale translation or page reuse.
- A full address-space flush is the correctness fallback when a bounded
  per-page invalidation request cannot represent the change.

The lock graph contains an explicit `TLB_WAIT` barrier:

```text
process/scheduler
  -> address-space/VM
  -> handle
  -> IPC/service
  -> VFS/device
  -> release every ordinary subsystem spinlock
  -> TLB_WAIT
  -> reacquire only after all acknowledgements
```

`TLB_WAIT` is not a lock class and cannot be nested inside one. Runtime
assertions require ordinary lock depth zero on entry and reject VM/process
lock acquisition from the shootdown IPI handler.

### Locking And Diagnostics

- Disabling interrupts affects only the local CPU; it is not mutual exclusion
  against another CPU.
- `spin_lock_irqsave()`/`spin_unlock_irqrestore()` is the default ordinary
  kernel spinlock contract. Acquire saves local IF, increments the CPU-local
  preemption-disable count, atomically claims the lock, and records owner CPU
  plus LIFO lock-stack position. Release verifies owner and LIFO order,
  publishes unlock, decrements preemption disable, and restores the matching
  saved IF state.
- Recursive acquisition, unlock by another CPU, out-of-order release, and
  token reuse are hard diagnostic failures; recursive acquisition never
  returns success as though a second lock were obtained.
- A plain or raw spinlock variant is allowed only for named low-level paths
  whose interrupt/preemption state is already controlled. NMI/Double Fault
  cannot acquire ordinary locks; IPI handlers use only bounded CPU-local
  atomics or an explicitly audited raw primitive.
- Spinlock recursion/order tracking moves to CPU-local storage before APs use
  ordinary kernel locks.
- Sleeping, user copy, allocation, and unbounded scans remain forbidden while
  a spinlock is held.
- Yield, wait, block, ordinary schedule, and context switch assert that
  ordinary lock depth and preemption-disable count are zero. The scheduler's
  internal handoff releases its lock before switching contexts.
- Process, scheduler, VM, handle, IPC/service, VFS/device, input, and graphics
  lock order is documented and tested as the audit progresses.
- Diagnostics expose logical CPU, APIC identity, lifecycle, current
  PID/TID/generation, idle/running state, interrupt/preemption depth, IPI/TLB
  counters, and queue ownership without exposing kernel addresses.

## 4.6A: CPU Topology And SMP Contracts

Goal: make every possible CPU a bounded, identity-safe kernel object before
changing execution behavior.

Implementation:

- retain enabled MADT Local APIC processor identities instead of only counting
  them;
- validate duplicate, disabled, malformed, overflow, BSP, xAPIC, and
  unsupported x2APIC cases;
- add the bounded CPU table, logical-ID mapping, lifecycle transitions, and
  authoritative lookup paths;
- identify the running BSP by hardware APIC identity and assign logical CPU 0;
- add topology and CPU-state diagnostics without kernel pointers;
- freeze CPU state, capacity, feature flags, and future public topology ABI
  sizes before exposing them.

Exit gate: host tests cover valid and invalid MADT topology, identity mapping,
capacity, and lifecycle transitions. QEMU `-smp 1`, `-smp 2`, and `-smp 4`
report the expected discovered topology while execution remains BSP-only.

## 4.6B: Per-CPU State And Entry Infrastructure

Goal: remove global single-CPU entry assumptions while still running only the
BSP.

Implementation:

- add one `CpuLocal` record per bounded CPU and a constant-time current-CPU
  accessor;
- move current-thread, idle, execution-stack, interrupt-depth,
  preemption-disable, scheduling counters, and lock tracking to CPU-local
  ownership;
- make `current_thread()` and `current_process()` derive from the current
  CPU's authoritative thread;
- create per-CPU kernel entry stacks, GDT/TSS records, Ring 0 stack selection,
  and distinct NMI/Double Fault IST storage from static fixed-stride arrays;
- make syscall, IRQ, exception, and context-switch entry preserve the CPU-local
  base and select the current thread's Ring 0 stack;
- reserve kernel GS permanently, keep FSGSBASE/user GS unavailable, validate
  normal entry against the `CpuLocal` header, and add emergency IST-based
  identity recovery that does not trust only GS;
- standardize interrupt-saving spinlock tokens, CPU-local preemption counts,
  owner/LIFO checks, and schedule/block/yield assertions;
- migrate the BSP through the same initialization path that APs will use;
- prepare but do not release the low-memory AP trampoline and startup mailbox.

Exit gate: all Phase 4.5 and full closure tests pass on one CPU through the new
CPU-local accessors. CPU-local host tests prove independent lock stacks,
interrupt nesting, current-thread state, TSS/stack ownership, owner-CPU
spinlock checks, and emergency-stack identity recovery. Controlled QEMU NMI
and diagnostic Double Fault paths identify the correct BSP CPU without an
ordinary lock or scheduler dependency.

## 4.6C: Application Processor Bring-Up And Idle

Goal: start APs safely and keep them in a real CPU-local idle path without
running ordinary scheduled work.

Implementation:

- add x86_64 INIT/SIPI Local APIC commands and delivery-status handling;
- build the real-mode/protected-mode/long-mode AP trampoline and bounded
  startup mailbox;
- give each AP its startup stack, CR3, CPU-local base, GDT/TSS/IST, IDT, and
  Local APIC setup before online publication;
- detect and publish invariant-TSC/CPUID time capabilities plus the BSP
  PIT-based fallback epoch that every CPU may use for later self-calibration;
- use release/acquire startup and online handshakes with bounded timeouts;
- install a bounded allocation-free AP startup-ping IPI so the BSP can verify
  that each AP still accepts interrupts after publishing online;
- enter `sti; hlt` in an AP-local idle loop and maintain heartbeat/interrupt
  counters;
- keep AP scheduler participation disabled until 4.6D;
- release every failed startup allocation exactly once and retain a diagnostic
  failure state.

Exit gate: QEMU with one, two, and four vCPUs boots reliably. Every requested AP
publishes the correct identity, enters its idle path, and acknowledges repeated
startup-ping IPIs; single-CPU mode remains identical; forced startup timeout
falls back without panic, leaked stack, or false online state.

## 4.6D: Multicore Scheduler And Local Preemption

Goal: allow several CPUs to execute distinct runnable threads concurrently.

Implementation:

- centralize ready/running/waiting/terminal transitions under the global
  scheduler authority;
- add `running_cpu` ownership and assertions that reject double claim, queued
  running threads, and terminal selection;
- let each online CPU select from the global ready queue or enter its local
  idle context;
- make every CPU independently calibrate its Local APIC timer against the
  published invariant-TSC or PIT epoch, record the measured rate/error, and
  reject scheduler release when calibration is missing or outside tolerance;
- keep PIT global timekeeping single-owner while Local APIC timers perform
  local runtime accounting and quantum expiration;
- switch per-CPU TSS `RSP0`, address space, FS TLS, and current-thread state on
  every selection;
- exercise simultaneous user threads in separate processes and in a shared
  process whose mappings remain frozen until 4.6F;
- preserve the Phase 4.5 4/6/8-tick priority policy and bounded progress.

Exit gate: at least two QEMU CPUs execute different user threads concurrently
for repeated quanta. No thread is observed on two CPUs, no running thread is
queued, every CPU retains a valid current/idle state, and single-CPU
regressions remain green. All scheduler CPUs pass the same calibration
tolerance, and measured global time remains constant rather than scaling with
the `-smp` count.

## 4.6E: Reschedule IPI, Remote Wakeup, Affinity, And Distribution

Goal: make cross-CPU scheduling events prompt, generation-safe, and bounded.

Implementation:

- reserve and install a reschedule IPI vector with per-CPU pending state;
- send an IPI when a newly ready thread should preempt or wake an idle remote
  CPU;
- coalesce duplicate reschedule requests without losing a required wake;
- add bounded affinity masks and deterministic invalid/offline-mask errors;
- distribute eligible ready threads across online CPUs without requiring work
  stealing or per-CPU queues;
- define CPU-offline interaction with affinity and pending reschedule work;
- add per-CPU sent/received/coalesced/ignored IPI diagnostics.

Exit gate: remote semaphore, condition, IPC, input, timer, and join wakeups run
on an eligible CPU within a documented bound. Affinity is obeyed, idle CPUs are
woken, duplicate IPIs are harmless, and no wake or runnable thread is lost.

## 4.6F: TLB Shootdown And Address-Space Safety

Goal: make concurrent execution and mutation of an address space safe.

Implementation:

- add generation-tagged address-space identity, active/cached CPU masks, and a
  monotonic TLB generation;
- add bounded per-CPU invalidation mailboxes and a dedicated shootdown vector;
- support local page invalidation and full-address-space flush fallback;
- serialize one operation token per address space and publish request, target
  mask, generation, and acknowledgement ordering;
- implement the three phases explicitly: mutate and quarantine under VM lock;
  release all ordinary locks and enter `TLB_WAIT`; then reacquire, revalidate,
  and retire only after every acknowledgement;
- assert zero ordinary lock depth at `TLB_WAIT`, keep interrupts enabled and
  local preemption disabled while waiting, and make the IPI handler lock-free
  and allocation-free;
- apply the bounded timeout policy without false acknowledgement or premature
  release;
- cover map, unmap, heap growth/shrink, surface mapping, process exit, and
  shared-process thread races.

Exit gate: two or more CPUs repeatedly access and mutate the same address
space without stale translations, cross-generation acknowledgement, premature
page reuse, deadlock, or drift. Injected delayed and duplicate acknowledgements
remain deterministic. Runtime lock-graph assertions prove every TLB wait
occurs after ordinary subsystem locks are released.

## 4.6G: Kernel-Wide SMP Audit And Interrupt Ownership

Goal: make every subsystem reachable from concurrent CPUs obey one lock,
interrupt, and memory-ordering contract.

Implementation:

- audit scheduler/process, VM/PMM/heap, handles, IPC/services, sync objects,
  VFS/filesystems/drivers, input, surfaces, display, and window state;
- split or extend locks only when a measured correctness need exists and
  preserve a documented global order;
- include the non-lock `TLB_WAIT` barrier in the ordering graph and verify that
  no subsystem carries an ordinary lock across it;
- replace unsynchronized shared counters/flags with lock-protected or explicit
  atomic operations;
- keep blocking and allocation out of spinlock and IPI/IRQ contexts;
- audit every spinlock call site for irqsave/raw context, owner CPU,
  preemption-disable balance, recursive acquisition, LIFO release, and illegal
  schedule/block/yield while atomic;
- define one tested CPU owner for each external interrupt source and safe
  handoff rules before changing IOAPIC destinations;
- make process exit, service restart, GUI teardown, and device activity safe
  while sibling threads execute on other CPUs;
- extend diagnostics and fault injection for lock contention, remote exit,
  concurrent close, and CPU-owned interrupt paths.

Exit gate: all audited subsystems pass concurrent QEMU workloads on at least
two CPUs with zero lock-order, recursion, release, sleeping-in-atomic,
double-cleanup, stale-owner, or interrupt-ownership violation.

## 4.6H: Multicore Fault Injection, Soak, And Closure

Goal: certify SMP and every inherited single-CPU contract.

Implementation and verification:

- inject AP-stack, CPU-local, trampoline/mailbox, Local APIC timer, IPI queue,
  scheduler claim, and TLB-request failures;
- in a diagnostic build, inject an invalid CPU-local header/emergency-stack
  candidate, timer-calibration rejection, wrong-CPU spinlock release,
  recursive acquisition, schedule-while-atomic attempt, and TLB wait with a
  held ordinary lock;
- force AP startup timeout, remote wake/exit races, delayed shootdown
  acknowledgement, concurrent process fault, and service/GUI restart;
- run single-, dual-, and four-vCPU focused suites;
- run a minimum 60-second multicore churn soak containing thread creation,
  synchronization, address-space mutation, IPC, services, GUI, and input;
- require per-CPU progress counters, no double-running thread, no stale ready
  entry, and identical warmed/final resource snapshots;
- add `make test-phase46` only after every invoked target exists;
- run a clean parallel normal/diagnostic UEFI build, `make test-phase45`,
  `make test-phase46`, and the full closure suite;
- record exact commands, vCPU counts, duration, cycle counts, resource tuples,
  and immutable implementation commits.

Exit gate: every Phase 4.6 matrix row and every inherited regression passes
from a clean tree. The required multicore soak has zero unexplained drift or
CPU stall, and closure evidence is committed.

## Required Order

```text
4.6A topology and contracts
  -> 4.6B per-CPU state and entry
  -> 4.6C AP bring-up and idle
  -> 4.6D multicore scheduling and local preemption
  -> 4.6E reschedule IPI, remote wake, affinity
  -> 4.6F TLB shootdown and address-space safety
  -> 4.6G subsystem audit and interrupt ownership
  -> 4.6H fault, soak, regression, closure
  -> Phase 5 desktop foundation
```

Starting APs before 4.6B would let several CPUs corrupt global entry and lock
tracking state. Allowing concurrent mapping mutation, teardown, or page reuse
before 4.6F would let stale TLB entries outlive page mappings. Each boundary
therefore has an explicit release gate.

## Phase Exit Gate

Phase 4.6 is complete only when at least two CPUs can concurrently execute,
preempt, block, remotely wake, migrate, fault, and terminate eligible threads;
shared address spaces survive generation-checked TLB shootdown; all audited
subsystems retain exact lifecycle and resource invariants; single-CPU behavior
remains supported; and the required multicore soak plus clean aggregate/full
closure suites pass with immutable evidence.
