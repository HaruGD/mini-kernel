# Phase 4.6 Regression Matrix

This matrix defines the evidence required to close SMP and multicore
scheduling. Every row is currently planned. Target names marked `Reserved`
are design names only and must not be cited as executed evidence until the
Makefile contains them and they pass.

The final aggregate target will be `make test-phase46`. It must invoke real
focused targets and must not be added as an empty or documentation-only
target.

## Matrix

| ID | Subphase | Contract | Planned automated evidence | Pass condition | Status |
| --- | --- | --- | --- | --- | --- |
| P46-R01 | 4.6A | MADT CPU topology has bounded capacity, stable logical/APIC identity, legal lifecycle transitions, and explicit unsupported/overflow handling. | Reserved: `make test-cpu-topology` | Valid 1/2/4-CPU tables map exactly; malformed, duplicate, disabled, overflow, and unsupported x2APIC entries cannot become runnable CPUs. | Planned |
| P46-R02 | 4.6A | QEMU topology discovery matches requested vCPU counts without changing BSP-only execution. | Reserved: `make test-smp-topology` | `-smp 1`, `-smp 2`, and `-smp 4` report the expected BSP/AP identities and no false online AP. | Planned |
| P46-R03 | 4.6B | Current thread, entry nesting, interrupt/preemption depth, lock stack, TSS, and idle state are CPU-local. | Reserved: `make test-percpu` | Independent CPU records cannot overwrite one another; BSP one-CPU regressions pass through the CPU-local accessor. | Planned |
| P46-R04 | 4.6C | AP startup publishes `ONLINE` only after valid stack, CR3, descriptor, interrupt, APIC, and idle state. | Reserved: `make test-ap-bringup` | Requested 1/2/4-vCPU boots have exact online counts, idle entry, and repeated startup-ping acknowledgements; forced timeout leaves a clean failed/offline record. | Planned |
| P46-R05 | 4.6D | A runnable thread is queued or running on exactly one CPU and never executes concurrently with itself. | Reserved: `make test-smp-scheduler` | Two or more CPUs run distinct eligible threads; atomic ready/running/wait/terminal transitions reject double claim, stale selection, and queued-running state. | Planned |
| P46-R06 | 4.6D | Per-CPU Local APIC preemption preserves priority, runtime attribution, and single-owner wall time. | Reserved: `make test-smp-preemption` | Every scheduler CPU preempts locally; global time does not scale with CPU count; low/normal/high progress stays within the documented bound. | Planned |
| P46-R07 | 4.6E | Remote wake and reschedule IPIs publish work before notification and cannot lose or duplicate a wake. | Reserved: `make test-smp-ipi` | Idle/busy remote CPUs receive bounded reschedule requests; coalesced/duplicate IPIs are harmless; semaphore, condition, IPC, input, timer, and join wakeups complete once. | Planned |
| P46-R08 | 4.6E | Affinity and distribution select only online eligible CPUs and cannot strand runnable work. | Reserved: `make test-smp-affinity` | Invalid/empty/offline masks fail; pinned threads remain eligible only on allowed CPUs; unpinned workload makes measured progress on multiple CPUs. | Planned |
| P46-R09 | 4.6F | Mapping mutation and address-space teardown invalidate every CPU that may cache the old translation before page reuse. | Reserved: `make test-tlb-shootdown` | Page/full flush requests use matching address-space generation and target mask; delayed, duplicate, stale, and timeout acknowledgements are deterministic and no stale mapping survives. | Planned |
| P46-R10 | 4.6G | Kernel subsystems obey SMP-safe lock, atomic, blocking, and interrupt ownership rules. | Reserved: `make test-smp-concurrency` | Concurrent process/VM/handle/IPC/service/VFS/input/graphics work has zero lock-order, recursion, release, sleeping-in-atomic, stale-owner, or duplicate-cleanup violation. | Planned |
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
- double scheduler claim, ready-plus-running duplication, terminal selection,
  current-CPU mismatch, invalid affinity, and offline-only affinity;
- reschedule IPI before work publication, duplicate/coalesced IPI, IPI to an
  offline CPU, remote wake versus timeout/cancel/exit, and idle wake races;
- TLB request exhaustion, stale address-space generation, wrong target mask,
  delayed/duplicate acknowledgement, target failure, page-free-before-ack,
  and address-space reuse;
- concurrent close/exit/unmap/service restart, interrupt ownership change,
  lock-order inversion, recursion, release by the wrong CPU, sleeping while
  atomic, and allocation from an IPI handler;
- one-vCPU fallback after every SMP-specific feature is enabled.

## Evidence Rules

- Replace `Reserved` or `Future` with exact real commands only after the
  corresponding targets exist and pass.
- Host tests close parsers and isolated state machines, but cannot alone close
  AP startup, interrupt entry, concurrent scheduling, IPI, TLB, or end-to-end
  resource rows.
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
