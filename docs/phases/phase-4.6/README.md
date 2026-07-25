# Phase 4.6: SMP And Multicore Scheduling

Phase 4.6 changes OS64 from a correct single-CPU threaded system into a
correct multicore system. Application processors become real execution
engines only after CPU identity, per-CPU state, interrupt entry, scheduler
ownership, and memory invalidation contracts are explicit.

The first implementation is correctness-first. It uses a bounded CPU table
and a locked global ready queue before considering per-CPU queues, work
stealing, NUMA policy, or lock-free scheduling.

## Documents

- [Entry baseline](entry_baseline.md): the exact Phase 4.5 exit state and the
  single-CPU assumptions that must be removed.
- [Implementation plan](implementation_plan.md): subphases 4.6A through 4.6H,
  fixed SMP policies, ordering, and exit gates.
- [Regression matrix](regression_matrix.md): required topology, AP startup,
  scheduler, IPI, TLB, subsystem, fault, and soak coverage.
- [Progress ledger](progress.md): live state and immutable implementation
  evidence.
- [Scheduler modernization plan](../../architecture/scheduler_modernization.md):
  the larger Phase 4H through Phase 4.6 sequence.

## Current Status

Phase 4.5 completed on 2026-07-25 with implementation commit `9acb245` and
evidence commit `7e7c7ea`. Its single-CPU thread lifecycle, synchronization,
fault rollback, required 60-second soak, aggregate suite, and full project
closure pass.

Phase 4.6A and 4.6B completed on 2026-07-25. ACPI now retains a bounded CPU
topology, the BSP uses logical CPU 0 through the same permanent-kernel-GS
`CpuLocal` path prepared for APs, and per-CPU GDT/TSS, Ring 0, NMI, and Double
Fault stacks are established. That closure left AP records prepared and
offline; 4.6C now implements their bounded startup and validation.

Phase 4.6C is complete: requested APs validate and publish online, acknowledge
startup IPIs and targeted NMI, and remain in CPU-local idle. The next task is
4.6D multicore scheduling and Local APIC timer calibration; no AP enters the
global scheduler before that release gate.

## Scope Boundary

Phase 4.6 includes:

- bounded CPU topology and stable logical CPU identities;
- per-CPU current thread, idle context, kernel entry state, interrupt depth,
  preemption state, lock tracking, GDT/TSS, and diagnostic counters;
- NMI/Double Fault-safe CPU-local recovery with per-CPU emergency IST stacks;
- x86_64 INIT/SIPI application-processor startup with bounded failure;
- simultaneous scheduling of user threads on at least two CPUs;
- calibrated Local APIC timer preemption, reschedule IPIs, remote wakeups,
  affinity, and bounded distribution;
- lock-safe three-phase cross-CPU TLB invalidation, quarantine, and safe
  address-space teardown;
- kernel-wide SMP lock and interrupt-ownership audits;
- deterministic fault injection and multicore soak closure.

It does not include CPU hotplug, suspend/resume, x2APIC mode, NUMA placement,
real-time scheduling, work stealing, tickless operation, PCID optimization,
or a desktop shell. Those are later extensions and must not weaken the first
SMP correctness contract.
