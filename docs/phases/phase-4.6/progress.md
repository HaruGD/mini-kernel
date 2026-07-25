# Phase 4.6 Progress

This is the live implementation ledger for SMP and multicore scheduling.
Stable design belongs in [implementation_plan.md](implementation_plan.md);
required coverage belongs in [regression_matrix.md](regression_matrix.md).
This file records only work that has actually started or completed.

## Status Definitions

- `Planned`: no implementation claim has been made;
- `In progress`: implementation has started but the exit gate has not passed;
- `Blocked`: a named unmet dependency prevents useful progress;
- `Complete`: every exit-gate requirement passed and an evidence record exists.

A plan or reserved Make target is not implementation evidence. `Complete`
requires immutable implementation commits, exact verification commands,
QEMU CPU counts, measured results, resource accounting where applicable, and
a separate evidence commit.

## Current Status

| Subphase | Status | Started | Completed | Implementation commit(s) | Evidence |
| --- | --- | --- | --- | --- | --- |
| 4.6A: CPU topology and SMP contracts | Planned | - | - | - | - |
| 4.6B: Per-CPU state and entry infrastructure | Planned | - | - | - | - |
| 4.6C: Application processor bring-up and idle | Planned | - | - | - | - |
| 4.6D: Multicore scheduler and local preemption | Planned | - | - | - | - |
| 4.6E: Reschedule IPI, remote wake, affinity, distribution | Planned | - | - | - | - |
| 4.6F: TLB shootdown and address-space safety | Planned | - | - | - | - |
| 4.6G: Kernel-wide SMP audit and interrupt ownership | Planned | - | - | - | - |
| 4.6H: Multicore fault injection, soak, and closure | Planned | - | - | - | - |

Current status: Phase 4.6 entry documentation is established. 4.6A CPU
topology and SMP contracts are the next implementation task. Phase 5 remains
gated on complete 4.6 closure.

## Recording Workflow

For each subphase:

1. change its row to `In progress` when implementation begins;
2. implement bounded behavior and focused positive/negative tests;
3. run the subphase exit gate and every affected earlier suite;
4. commit implementation and tests;
5. append an evidence record using the immutable implementation hash;
6. change the row and matching roadmap item to `Complete` only after every
   required result passes;
7. commit the evidence update separately.

The evidence commit is separate because it cannot contain its own final hash.
Do not record predicted commit IDs.

## Evidence Record Format

Append one section per completed subphase:

```text
## 4.6X: Subphase Name

- Status: Complete
- Started: YYYY-MM-DD
- Completed: YYYY-MM-DD
- Implementation commit(s): `<immutable hashes>`

### Delivered

- concrete CPU, per-CPU, scheduler, IPI, TLB, or audit result;
- compatibility and failure-policy result;
- diagnostics and focused tests delivered.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make <real-focused-target>` | PASS | vCPU count, online CPUs, cycles, counters |
| `make <affected-regression>` | PASS | exact result |

### Resource And CPU Accounting

- QEMU CPU model and `-smp` count;
- discovered/prepared/online/failed CPU counts;
- per-CPU timer, scheduler, IPI, TLB, and idle progress;
- warmed and final process/thread/page/heap/resource tuple;
- unexplained drift or stalled CPU: zero, or a documented blocker.

### Remaining

- work assigned to the next subphase;
- accepted limitation already named in the plan.
```

## Phase 4.5 Entry Evidence

Phase 4.5 is the immutable starting point, not Phase 4.6 completion evidence.
It closed on 2026-07-25 with implementation commit `9acb245` and evidence
commit `7e7c7ea`.

The clean Phase 4.5 aggregate and full project closure passed. The required
60-second thread soak completed 14 composite cycles, 42 thread programs, and
56 GUI window lifecycles with identical warmed/final tuples:
`(4,21,1,0,4,0,1,27183,1944432,1961984)`. Thread-specific fault injection
also ended with identical resources, an empty ready queue, and no lock
violation.

## Open Implementation Order

```text
4.6A topology/contracts
  -> 4.6B per-CPU state/entry
  -> 4.6C AP startup/idle
  -> 4.6D multicore scheduling/preemption
  -> 4.6E IPI/remote wake/affinity
  -> 4.6F TLB shootdown
  -> 4.6G subsystem audit/interrupt ownership
  -> 4.6H fault/soak/closure
```

No subphase has implementation evidence yet.
