# Phase 4.6 Entry Baseline

This document records the state from which SMP and multicore scheduling start.
It is a baseline, not a claim that application processors already execute
OS64 code.

## Entry Gate

Phase 4.5 closed on 2026-07-25.

- implementation commit: `9acb245`;
- closure evidence commit: `7e7c7ea`;
- `make test-phase45`: PASS;
- clean `make test-closure`: PASS;
- required 60-second thread soak: 14 composite cycles, 42 thread programs,
  and 56 GUI window lifecycles;
- warmed/final thread-soak resource tuple:
  `(4,21,1,0,4,0,1,27183,1944432,1961984)` with zero drift;
- six thread-specific injected failure paths and the fatal sibling-fault test
  return to an identical resource tuple with an empty ready queue and no lock
  violation.

The optional one-hour release soak is not an entry requirement for Phase 4.6.

## Current CPU And Interrupt Model

OS64 boots one processor: the bootstrap processor (BSP).

- ACPI MADT parsing counts enabled processor entries but discards their ACPI
  processor IDs and Local APIC IDs.
- The x86_64 interrupt backend maps and enables the BSP Local APIC and the
  first IOAPIC when available.
- IOAPIC IRQ 0 and IRQ 1 destinations are the BSP. The legacy PIC remains the
  fallback when APIC initialization is unavailable.
- There is no CPU table, logical CPU identity, AP startup trampoline,
  INIT/SIPI sequence, AP online handshake, IPI vector, Local APIC scheduler
  timer, or CPU-local idle context.
- PIT IRQ 0 is the only scheduler/timekeeping interrupt source. The global
  clock must not advance once per CPU when Local APIC timers are introduced.

Finding multiple MADT processor entries therefore means only that firmware
described multiple CPUs. It does not mean those CPUs are online.

## Current Scheduler And Thread Model

Phase 4.5 provides 32 bounded thread records: eight process records with four
threads per process. The ready queue also has 32 entries.

The queue contains `Thread*` values and is protected by the process/scheduler
spinlock, but the following state is still global:

- `user_program_depth`;
- process and thread execution stacks;
- current-thread selection through the global thread stack;
- ready-queue head/count and aggregate scheduling counters;
- one active TSS, Ring 0 stack selector, and double-fault IST;
- spinlock held-depth and held-lock stack.

These globals are valid only because one CPU executes kernel and user code.
They must become per-CPU or remain deliberately shared under a lock before an
AP reaches normal interrupt, scheduler, syscall, or fault paths.

## Current Lock And Memory Model

Kernel spinlocks use atomic exchange with acquire/release ordering and disable
interrupts locally while held. The lock-class order is already checked.
However, non-host lock tracking is global rather than CPU-local, so a second
CPU would corrupt recursion/order diagnostics.

Address spaces and page tables have no active-CPU mask, TLB generation,
shootdown request, acknowledgement, or deferred page-free rule. A mapping can
currently be invalidated only on the executing CPU. Running one address space
on two CPUs before shootdown exists would permit stale translations and unsafe
physical-page reuse.

## Inherited Invariants

Every Phase 4 and Phase 4.5 lifecycle rule remains binding:

- PID/TID references include a nonzero generation;
- a thread is in at most one of ready, running, waiting, or terminal state;
- terminal or stale threads are never queued or resumed;
- every wait, wake, exit, join, and resource release completes exactly once;
- process-owned resources are destroyed only after all owned threads stop;
- explicit thread exit is local, while a fatal shared-address-space user fault
  terminates and cleans the process;
- shell, services, GUI, input, and display progress do not depend on a
  foreground scheduler helper;
- warmed/final resource drift is zero for required closure workloads.

SMP adds the following invariants without weakening those rules:

- one live thread may be `RUNNING` on at most one logical CPU;
- one logical CPU has at most one current thread and one idle context;
- a CPU publishes `ONLINE` only after its CPU-local state, descriptor tables,
  interrupt stack, Local APIC, and idle path are usable;
- an offline or failed CPU owns no running thread, queued shootdown, or
  unreleased startup resource;
- a physical page is not freed or reused until every CPU that could cache its
  translation acknowledges invalidation.

## Entry Constraints

- Retain a single-CPU boot and test mode throughout the phase.
- Bring up APs into a bounded idle loop before allowing them to schedule user
  or general kernel work.
- Move lock tracking and interrupt-entry state to per-CPU storage before APs
  acquire ordinary kernel locks.
- Begin with one locked global ready queue. Optimization is not an entry
  requirement.
- Keep external device interrupts on the BSP until an explicit owner and
  migration policy is tested.
- Keep PIT-based global timekeeping single-owner. Per-CPU Local APIC timer
  ticks drive local preemption and must not multiply wall-clock time.
- Before 4.6F, concurrent execution may use only mappings held stable for the
  whole run. Do not permit concurrent mapping mutation, unmap, teardown, or
  page reuse until shootdown and acknowledgement are implemented.
- Never wait for another CPU while holding a lock that its IPI handler needs.
- Do not expose public affinity or topology ABI until sizes, flags, and denial
  behavior are frozen.
- Every subphase must keep the one-CPU Phase 4.5 and full closure suites green.

## Entry Decision

All Phase 4.5 entry evidence is present. Phase 4.6A may begin. Phase 5 remains
gated on the complete Phase 4.6 exit gate.
