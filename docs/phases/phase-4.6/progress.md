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
| 4.6A: CPU topology and SMP contracts | Complete | 2026-07-25 | 2026-07-25 | `fb7f67f` | P46-R01, P46-R02 |
| 4.6B: Per-CPU state and entry infrastructure | Complete | 2026-07-25 | 2026-07-25 | `01a0408` | P46-R03 |
| 4.6C: Application processor bring-up and idle | Planned | - | - | - | - |
| 4.6D: Multicore scheduler and local preemption | Planned | - | - | - | - |
| 4.6E: Reschedule IPI, remote wake, affinity, distribution | Planned | - | - | - | - |
| 4.6F: TLB shootdown and address-space safety | Planned | - | - | - | - |
| 4.6G: Kernel-wide SMP audit and interrupt ownership | Planned | - | - | - | - |
| 4.6H: Multicore fault injection, soak, and closure | Planned | - | - | - | - |

Current status: the SMP foundation bundle, 4.6A and 4.6B, is complete. AP
records are prepared but remain offline. 4.6C AP bring-up and CPU-local idle
is next. Phase 5 remains gated on complete 4.6 closure.

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
- per-CPU timer calibration source/frequency/error, scheduler, IPI, TLB, and
  idle progress;
- NMI/Double Fault CPU identity and emergency-entry result;
- spinlock IF/preemption/owner violations and `TLB_WAIT` lock-depth result;
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

## 4.6A: CPU Topology And SMP Contracts

- Status: Complete
- Started: 2026-07-25
- Completed: 2026-07-25
- Implementation commit: `fb7f67f`

### Delivered

- MADT Local APIC and x2APIC processor records are retained with a fixed
  eight-CPU capacity, explicit disabled/unsupported/overflow/malformed counts,
  and no ID truncation.
- Dense logical IDs place the hardware-identified BSP at logical CPU 0.
  Duplicate identities, BSP mismatch, illegal lifecycle transitions, and
  x2APIC-only CPUs cannot become runnable.
- `cpus` and diagnostic boot expose identities, lifecycle, and bounded
  topology counters without kernel addresses.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-cpu-topology` | PASS | Synthetic valid 1/4 CPU, disabled, duplicate, malformed, nine-entry overflow, BSP mismatch, and x2APIC-deferred cases |
| `make test-smp-topology` | PASS | QEMU `-cpu max`, `-smp 1/2/4`: records `1/2/4`, online `1`, no false-online AP |
| `make test-phase1` | PASS | Existing ACPI parsing and corruption/fallback sessions remain valid |

### Remaining

- x2APIC mode and CPU hotplug remain deferred by policy.
- APs are identity-safe records only; 4.6C owns INIT/SIPI and online
  publication.

## 4.6B: Per-CPU State And Entry Infrastructure

- Status: Complete
- Started: 2026-07-25
- Completed: 2026-07-25
- Implementation commit: `01a0408`
- Regression-support commit: `2c4d669`

### Delivered

- The BSP now uses a validated permanent-kernel-GS `CpuLocal`; FSGSBASE is
  disabled, GDT reload does not overwrite GS, and user TLS remains FS-based.
- CPU-local execution/current-thread state, interrupt and entry depth,
  preemption depth, lock stack, counters, and AP-prepared static kernel stacks
  replace the former single global execution stack.
- Each bounded CPU owns a GDT/TSS association and separate 16 KiB aligned Ring
  0, NMI IST, and Double Fault IST stacks. Emergency identity recovery
  validates stack range, magic, self pointer, logical ID, and APIC identity.
- Ordinary irqsave spinlocks now record owner CPU and LIFO position, balance
  CPU-local preemption depth, reject recursion and wrong-CPU release, and
  expose schedule-while-atomic assertions and counters.
- A diagnostic `debugfault df` path and QEMU NMI injection exercise the
  nonallocating emergency paths. AP execution/NMI remains assigned to 4.6C.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-percpu` | PASS | Four independent records/stacks; invalid magic/range and nested NMI rejected; AP records prepared/offline |
| `make test-spinlocks` | PASS | IF/preemption balance, ordering, recursion rejection, wrong-CPU rejection, 40,000 contended increments |
| `make test-smp-emergency-entry` | PASS | QEMU `-cpu max -smp 4`: BSP NMI count `1`; diagnostic Double Fault resolved logical `0`, APIC `0`, then halted |
| `make test-phase46-foundation` | PASS | 4.6A/B host tests plus 1/2/4-vCPU topology and emergency QEMU sessions |
| `make test-phase45` | PASS | Required 60-second soak: 14 cycles, 42 thread programs, 56 GUI windows; warmed/final tuple identical `(4,21,1,0,4,0,1,27183,1944432,1961984)` |
| `make test-closure` | PASS | Full inherited project closure; GUI soak 35 cycles/140 windows/7 restarts with identical resources, service soak 41 cycles |
| `make clean`; `make uefi-diagnostic -j4` | PASS | Clean parallel diagnostic UEFI image build |

### Resource And CPU Accounting

- Topology sessions: QEMU `-cpu max`, `-smp 1/2/4`; discovered records matched
  the request, exactly one BSP remained online, and AP records transitioned
  only to prepared.
- Emergency session: QEMU `-cpu max -smp 4`; BSP NMI returned with interrupt
  depth restored to zero. Double Fault used its separate IST and halted after
  logical/APIC identity output. Actual AP emergency entry is deferred until
  AP startup in 4.6C.
- Required Phase 4.5 and closure soak resource tuples had zero final drift.
  Spinlock violation counters were zero in normal end-to-end execution.

### Remaining

- 4.6C must install the CPU-local base, GDT/TSS/IST, IDT, Local APIC, and idle
  stack on each AP before publishing it online.
- AP scheduling, Local APIC timer calibration, reschedule IPIs, and TLB
  shootdown remain later subphases.
