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
| 4.6C: Application processor bring-up and idle | Complete | 2026-07-25 | 2026-07-25 | `d2c4694` | P46-R04 |
| 4.6D: Multicore scheduler and local preemption | Complete | 2026-07-25 | 2026-07-25 | `d8d1787`, `4595489`, `02abf86`, `d17075e` | P46-R05, P46-R06 |
| 4.6E: Reschedule IPI, remote wake, affinity, distribution | In progress | 2026-07-25 | - | `d8d1787`, `4595489` | P46-R07, P46-R08 partial |
| 4.6F: TLB shootdown and address-space safety | Planned | - | - | - | - |
| 4.6G: Kernel-wide SMP audit and interrupt ownership | Planned | - | - | - | - |
| 4.6H: Multicore fault injection, soak, and closure | Planned | - | - | - | - |

Current status: 4.6A through 4.6D are complete. Four-vCPU QEMU runs three
distinct pinned user threads concurrently on AP1/AP2/AP3 with atomic
single-CPU claims and calibrated CPU-local quantum accounting. 4.6E has its
reschedule IPI, affinity ABI, distribution policy, diagnostics, and remote
semaphore/join path implemented, but remains in progress until the complete
condition/IPC/input/timer remote-wake gate is repeatable. Phase 5 remains
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

## 4.6C: Application Processor Bring-Up And Idle

- Status: Complete
- Started: 2026-07-25
- Completed: 2026-07-25
- Implementation commit: `d2c4694`

### Delivered

- The BSP installs a 256-byte low-memory trampoline and bounded mailbox, then
  starts each prepared xAPIC processor sequentially with INIT assert/deassert
  and two SIPIs.
- The trampoline establishes protected mode, PAE, NXE, long mode, the shared
  CR3, and the AP's prepared kernel stack before entering C.
- Each AP validates its logical/APIC identity, activates permanent kernel GS,
  installs its per-CPU GDT/TSS/Ring 0/NMI/Double Fault stacks, loads the IDT,
  enables its Local APIC, and only then publishes `ONLINE` with release
  ordering.
- Online APs acknowledge three allocation-free fixed IPIs apiece and remain
  in `sti; hlt` CPU-local idle. Scheduler participation remains disabled until
  4.6D.
- BSP diagnostics expose attempted/online/failed APs, ping counts, idle wake
  counts, and the invariant-TSC/CPUID or PIT-fallback time reference without
  exposing kernel addresses.
- A diagnostic `cpunmi <logical-id>` path sends an actual Local APIC NMI to a
  selected online AP and waits for its per-CPU IST acknowledgement.
- Startup timeout/state tests prove that a missing AP remains offline in a
  named `FAILED` state, cannot publish late online, and retains its statically
  owned startup stack without allocation drift.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-ap-bringup` | PASS | QEMU `-cpu max -smp 4`: four CPUs online; APs acknowledged 3 pings each; AP1 handled NMI on its own IST; BSP shell remained responsive |
| `make test-smp-topology` | PASS | QEMU `-cpu max -smp 1/2/4`: exact online totals `1/2/4`; AP attempted/online `0/0`, `1/1`, `3/3`; ping sent/ack `0/0`, `3/3`, `9/9`; zero failures |
| `make test-ap-startup-state` | PASS | Forced AP timeout: `PREPARED -> STARTING -> FAILED`, BSP-only online count `1`, false/late online rejected, stack ownership unchanged |
| `make test-phase46-foundation test-ap-bringup` | PASS | 4.6A/B host contracts, irqsave spinlocks, BSP emergency paths, exact AP topology, AP interrupt/idle path, and negative startup state all passed |
| `make test-phase45` | PASS | Required 60-second inherited soak: 14 cycles, 42 thread programs, 56 GUI windows; warmed/final tuple identical `(4,21,1,0,4,0,1,27183,1944432,1961984)` |
| `make clean`; `make uefi-diagnostic -j4` | PASS | Clean parallel diagnostic UEFI image build with the embedded trampoline |

### Resource And CPU Accounting

- QEMU used `-cpu max` with `-smp 1`, `2`, and `4`. Every discovered record
  became online in the positive sessions; failed AP count was zero.
- The four-vCPU AP-NMI session recorded AP1 NMI count `1`, AP ping count `3`,
  all four lifecycle records online, and a responsive BSP shell afterward.
- QEMU did not expose usable CPUID `0x15`/`0x16` TSC frequency data in these
  sessions, so diagnostics selected the explicit PIT-epoch fallback source.
- The deterministic timeout model kept the AP offline and left the BSP online;
  startup storage is static, so no allocation or release drift occurred.
- The inherited Phase 4.5 resource tuple had zero warmed-to-final drift.

### Remaining

- APs deliberately run only their CPU-local idle loop; 4.6D owns scheduler
  release, Local APIC timer self-calibration, and concurrent thread execution.
- Reschedule IPI, affinity, TLB shootdown, subsystem-wide SMP auditing, and
  multicore soak remain 4.6E through 4.6H.

## 4.6D: Multicore Scheduler And Local Preemption

- Status: Complete
- Started: 2026-07-25
- Completed: 2026-07-25
- Implementation commits: `d8d1787`, `4595489`, `02abf86`, `d17075e`

### Delivered

- The locked global ready queue now atomically removes and claims one eligible
  thread for exactly one logical CPU. `running_cpu`, `last_cpu`, affinity, and
  migration accounting make queued/running duplication and double claim
  observable and rejectable.
- AP idle loops enter the scheduler only after the release gate. Three
  shared-process workers can remain pinned to AP1/AP2/AP3 and execute
  concurrently while their existing mappings remain frozen.
- User entry/return state, saved FPU/SIMD state, current-thread selection,
  kernel return stack, FS TLS, and TSS `RSP0` are CPU/thread-local on every
  scheduler CPU.
- Every scheduler CPU calibrates a periodic Local APIC timer against the
  BSP-published TSC/PIT reference before release. Local timers own runtime and
  quantum accounting; PIT IRQ0 remains the only global time/timeout owner.
- Runnable notification never sends a self-IPI. This preserves the one-vCPU
  fallback and prevents needless nested local preemption during thread
  creation.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-smp-scheduler` | PASS | Host model rejects invalid affinity and a second claim while the first CPU owns the thread |
| `make test-smp-timer` | PASS | QEMU `-cpu max -smp 1/2/4`: scheduler CPUs `1/2/4`, zero calibration failures, every error within 1500 bps; PIT comparison deltas `255/244/243` in the final clean run |
| `make test-smp-preemption` | PASS | QEMU `-cpu max -smp 4`: low/normal/high workers observed CPU mask `0x0e`, maximum simultaneous workers `3`, Local APIC quantum expirations `4`, failures `0` |
| `make test-thread-abi` | PASS | One-vCPU `uthread_c.elf` passed 10 runs; queue remained empty and warmed/final PMM/heap/resource tuples were identical |
| `make test-phase45-abc` | PASS | Process/thread ABI, 91 SDK contracts, and drive-free scheduler/service-control regression passed |
| `make clean`; `make -j4 uefi-diagnostic`; `make test-smp-execution` | PASS | Clean image rebuilt; host ownership, 1/2/4 timer/topology, and four-vCPU concurrent execution all passed |

### Resource And CPU Accounting

- QEMU used `-cpu max` with `-smp 1`, `2`, and `4`. Every requested CPU was
  online and scheduler-enabled; calibration failure count was zero.
- The final four-vCPU workload recorded AP claims, user entries, Local APIC
  ticks, and received reschedule IPIs on AP1/AP2/AP3. No kernel fatal path was
  present.
- The one-vCPU lifecycle run completed ten thread-program cycles with
  identical warmed/final tuple
  `(0,0,0,0,0,0,0,27186,4120432,4141056)` and scheduler tuple `(0,8)`.
- The same host interval produced comparable PIT deltas for one, two, and four
  vCPUs rather than a CPU-count multiplier.

### Remaining

- 4.6E must close every remote wake class and unpinned distribution gate.
- Concurrent mapping mutation, teardown, and physical-page reuse remain
  forbidden until generation-checked 4.6F TLB shootdown exists.

## 4.6E: Reschedule IPI, Remote Wakeup, Affinity, And Distribution

- Status: In progress
- Started: 2026-07-25
- Implementation commits: `d8d1787`, `4595489`

### Delivered So Far

- Fixed vector `0xF3` carries bounded, allocation-free reschedule requests.
  Runnable state is published under the scheduler lock before notification;
  each CPU has pending, sent, received, coalesced, and ignored accounting.
- Round-robin notification selects an online eligible remote CPU and skips the
  sender. Duplicate pending requests coalesce.
- Thread ABI v2, syscall 107, and the SDK expose bounded affinity masks.
  Empty and offline-only masks fail; removing a running CPU requests a safe
  reschedule boundary.
- The four-vCPU workload proves remote semaphore release, pinned AP
  eligibility, idle wake, and join completion without a lost runnable thread.

### Verification So Far

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make test-smp-ipi` | PASS (partial gate) | AP1/AP2/AP3 receive reschedule IPIs and complete the semaphore-gated workload |
| `make test-smp-affinity` | PASS (partial gate) | Invalid/empty/offline host masks fail; pinned workers execute only on CPU1/CPU2/CPU3 |
| `make test-phase45-abc` | PASS | Existing single-CPU IPC and input blocking semantics remain green |

### Completion Blocker

- The existing recursive user-context runner cannot yet execute one combined
  repeated condition/IPC/input/timer remote-wake workload deterministically;
  sequential thread reuse can exhaust the AP's nested 16 KiB execution stack.
- 4.6E therefore remains `In progress`. It becomes complete only after a
  non-recursive context-switch boundary or isolated repeatable remote-wake
  sessions cover condition, IPC, input, timer, join, and unpinned
  distribution without weakening P46-R07/P46-R08.
