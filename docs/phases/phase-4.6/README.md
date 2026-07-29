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

Phase 4.6C and 4.6D are complete: requested APs validate and publish online,
then enter the released global scheduler only after their Local APIC timers
are calibrated. Three pinned user workers execute concurrently on AP1/AP2/AP3,
the PIT remains single-owner global time, and the one-vCPU fallback passes.

Phase 4.6E completed on 2026-07-26. Reschedule IPIs, affinity, pinned and
unpinned distribution, and semaphore/join, condition, timer, IPC, and input
remote wake paths pass in repeatable four-vCPU sessions.

Phase 4.6F completed on 2026-07-30. Address spaces now carry stable identities
and generations, one serialized mapping transaction, per-CPU shootdown
mailboxes, lock-free `TLB_WAIT`, and acknowledgement-gated page retirement.
Four-vCPU mapping churn and inherited surface/thread regressions pass.

Phase 4.6G completed on 2026-07-30. External IRQ0/IRQ1 and the system
service/GUI control plane have explicit CPU0 ownership. Process wait and
terminal transitions retain CPU ownership until the returning kernel path is
safe, handles/surfaces/display state are serialized, and user console records
are emitted atomically. The complete four-vCPU subsystem, service restart,
GUI crash/recovery, and input suite passes.

Phase 4.6H completed on 2026-07-30 with implementation commit `13fce61` and
closure regression fix `61a13bb`. Deterministic SMP fault injection, 200-cycle
four-vCPU synchronization stress, the required four-vCPU 60-second soak,
clean normal/diagnostic builds, the complete `test-phase46` aggregate, and the
full inherited closure suite pass. Phase 4.6 is closed and Phase 5 may begin;
the optional one-hour release soak was not required or run.

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
