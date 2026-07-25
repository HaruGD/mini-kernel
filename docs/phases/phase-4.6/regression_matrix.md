# Phase 4.6 Regression Matrix

This matrix defines the evidence required to close SMP and multicore
scheduling. Rows marked `Reserved` are design names only and must not be cited
as executed evidence until the Makefile contains them and they pass.

The final aggregate target will be `make test-phase46`. It must invoke real
focused targets and must not be added as an empty or documentation-only
target.

## Matrix

| ID | Subphase | Contract | Planned automated evidence | Pass condition | Status |
| --- | --- | --- | --- | --- | --- |
| P46-R01 | 4.6A | MADT CPU topology has bounded capacity, stable logical/APIC identity, legal lifecycle transitions, and explicit unsupported/overflow handling. | `make test-cpu-topology` | Valid 1/2/4-CPU tables map exactly; malformed, duplicate, disabled, overflow, and unsupported x2APIC entries cannot become runnable CPUs. | Complete |
| P46-R02 | 4.6A/4.6C | QEMU topology discovery and online state match requested vCPU counts. | `make test-smp-topology` | `-smp 1`, `-smp 2`, and `-smp 4` report the expected BSP/AP identities, exact online totals, zero startup failures, and three acknowledged startup pings per AP. | Complete |
| P46-R03 | 4.6B | Current thread, entry nesting, interrupt/preemption depth, lock stack, TSS/IST, and idle state are CPU-local; NMI/Double Fault can recover CPU identity without trusting only interrupted GS. | `make test-percpu`; `make test-smp-emergency-entry` | Independent CPU records cannot overwrite one another; permanent kernel GS cannot be redirected by user state; controlled BSP NMI and diagnostic Double Fault select the correct static IST/header or enter the minimal emergency halt path without ordinary locks. | Complete |
| P46-R04 | 4.6C | AP startup publishes `ONLINE` only after valid stack, CR3, descriptor, interrupt, APIC, and idle state. | `make test-ap-bringup`; `make test-ap-startup-state` | Requested 1/2/4-vCPU boots have exact online counts, idle entry, controlled AP NMI identity, and three startup-ping acknowledgements per AP; forced timeout leaves a clean failed/offline record and rejects late online publication. | Complete |
| P46-R05 | 4.6D | A runnable thread is queued or running on exactly one CPU and never executes concurrently with itself. | `make test-smp-scheduler`; `make test-smp-preemption` | Two or more CPUs run distinct eligible threads; atomic ready/running/wait/terminal transitions reject double claim, stale selection, and queued-running state. | Complete |
| P46-R06 | 4.6D | Per-CPU Local APIC calibration/preemption preserves priority, runtime attribution, bounded drift, and single-owner wall time. | `make test-smp-timer`; `make test-smp-preemption` | Each CPU self-calibrates against the published invariant-TSC or PIT reference and records frequency/error within tolerance before scheduler release; every scheduler CPU preempts locally; global time does not scale with CPU count; low/normal/high progress stays bounded. | Complete |
| P46-R07 | 4.6E | Remote wake and reschedule IPIs publish work before notification and cannot lose or duplicate a wake. | `make test-smp-ipi`; `make test-smp-remote-wake` | Idle/busy remote CPUs receive bounded reschedule requests; coalesced/duplicate IPIs are harmless; semaphore, condition, IPC, input, timer, and join wakeups complete once. | Complete |
| P46-R08 | 4.6E | Affinity and distribution select only online eligible CPUs and cannot strand runnable work. | `make test-smp-affinity`; `make test-smp-remote-wake` | Invalid/empty/offline masks fail; pinned threads remain eligible only on allowed CPUs; unpinned workload makes measured progress on multiple CPUs. | Complete |
| P46-R09 | 4.6F | Mapping mutation and address-space teardown invalidate every CPU that may cache the old translation before page reuse through a lock-safe three-phase transaction. | Reserved: `make test-tlb-shootdown`; `make test-tlb-lock-order` | Mutation quarantines state under VM lock, the initiator flushes locally, `TLB_WAIT` begins only at ordinary lock depth zero, remote acknowledgements match address-space operation generation/target mask, a CPU entering later flushes the newer generation before user mode, and retirement occurs only after every required acknowledgement; delayed, duplicate, stale, and timeout cases cannot free or reuse state. | Planned |
| P46-R10 | 4.6G | Kernel subsystems obey SMP-safe irqsave/raw spinlock, preemption, atomic, blocking, and interrupt ownership rules. | Reserved: `make test-smp-spinlocks`; `make test-smp-concurrency` | Acquire/release balances local IF and preemption depth, records/verifies owner CPU and LIFO order, and rejects recursion, wrong-CPU/token release, scheduling/blocking while atomic, ordinary NMI/DF locking, and locks crossing `TLB_WAIT`; concurrent subsystem work has zero violation. | Planned |
| P46-R11 | 4.6G | Existing shell, services, display, window, GUI, and device input remain responsive under multicore execution and restart. | Reserved: `make test-smp-services-gui` | Supervised service restart, GUI lifecycle, input focus, and display ownership pass on at least two CPUs with named interrupt ownership and no helper loop. | Planned |
| P46-R12 | 4.6H | SMP startup, scheduler, IPI, TLB, and teardown failures roll back without false online/running state or resource drift. | Reserved: `make test-smp-faults` | Every injected point fires exactly once; AP/startup failure degrades safely; remote fault/exit and delayed shootdown cleanup complete exactly once. | Planned |
| P46-R13 | 4.6H | Repeated multicore lifecycle, synchronization, mapping, IPC, service, GUI, and input churn is stable. | Reserved: `make test-smp-soak` | A minimum 60-second dual/four-vCPU soak reports per-CPU progress, cycle counts, identical warmed/final resources, no stalled CPU, and no stale queued/running thread. | Planned |
| P46-R14 | 4.6H | Phase 4.6 and all inherited single-CPU contracts pass together from a clean tree. | Future `make test-phase46`; existing `make test-phase45` and `make test-closure`; clean parallel UEFI build | Every focused row and inherited suite passes; exact commands, vCPU counts, durations, commits, and measurements are recorded. | Planned |

## Mandatory Negative Coverage

Focused targets must cover, where applicable:

- malformed MADT lengths, duplicate APIC IDs, disabled processors, topology
  overflow, BSP mismatch, and unsupported x2APIC-only identity;
- AP stack/CPU-local/trampoline/mailbox allocation failure, INIT/SIPI delivery
  failure, startup timeout, duplicate online publication, and late AP arrival;
- invalid/mismatched `CpuLocal` magic, self pointer, logical/APIC identity,
  emergency IST range, GS validation, NMI nesting, and Double Fault before
  online publication;
- double scheduler claim, ready-plus-running duplication, terminal selection,
  current-CPU mismatch, invalid affinity, and offline-only affinity;
- missing/invalid invariant-TSC data, CPUID frequency disagreement, PIT
  fallback, zero/overflowing LAPIC reload, per-CPU calibration outside
  tolerance, and CPU-count-dependent wall-clock advancement;
- reschedule IPI before work publication, duplicate/coalesced IPI, IPI to an
  offline CPU, remote wake versus timeout/cancel/exit, and idle wake races;
- TLB request exhaustion, stale address-space generation, wrong target mask,
  delayed/duplicate acknowledgement, target failure, page-free-before-ack,
  address-space reuse, local-initiator omission, a CPU entering the address
  space after target-mask capture, entering `TLB_WAIT` with interrupts
  disabled or any ordinary lock held, and VM/process lock acquisition from the
  shootdown IPI handler;
- concurrent close/exit/unmap/service restart, interrupt ownership change,
  lock-order inversion, recursion, wrong-CPU or non-LIFO/token release,
  unbalanced IF/preemption restoration, yield/block/schedule while atomic,
  ordinary lock acquisition from NMI/Double Fault, and allocation from an IPI
  handler;
- one-vCPU fallback after every SMP-specific feature is enabled.

## Evidence Rules

- Replace `Reserved` or `Future` with exact real commands only after the
  corresponding targets exist and pass.
- Host tests close parsers and isolated state machines, but cannot alone close
  AP startup, interrupt entry, concurrent scheduling, IPI, TLB, or end-to-end
  resource rows.
- 4.6B emergency-entry evidence includes controlled QEMU NMI on the BSP and a
  diagnostic Double Fault path. Controlled AP NMI becomes mandatory in 4.6C
  after AP execution exists; source inspection or a normal IRQ is not a
  substitute.
- Timer evidence records calibration source, per-CPU measured frequency/error,
  tolerance, and one/two/four-vCPU global elapsed-time comparison.
- TLB evidence records transaction generation, target/ack masks, ordinary
  lock depth at `TLB_WAIT`, and quarantine/retirement counts.
- QEMU evidence names the exact `-smp` count and CPU model used.
- Every concurrency row needs positive execution plus deterministic
  denial/race/failure coverage.
- Resource-sensitive tests record warmed and final tuples and per-CPU progress,
  not only a PASS marker.
- A passing aggregate cannot hide a failed focused row.
- P46-R01 through P46-R13 all block P46-R14, Phase 4.6 closure, and Phase 5
  entry.
- The optional one-hour SMP release soak is useful release evidence but is not
  a substitute for, or prerequisite of, the required repeatable 60-second
  soak.
