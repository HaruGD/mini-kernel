# Phase 3.5 Foundation Stabilization

Phase 3.5 hardens the kernel, process, IPC, and service foundations before the
compositor and window server are introduced. The goal is not abstract
perfection. The goal is a measurable platform that can host long-running GUI
services without process-slot leaks, kernel faults, stale identities, or
unbounded message copies.

## Working Rules

- Complete one numbered task per commit unless two tasks share one invariant.
- Keep all existing Phase 1 through Phase 3 tests passing.
- Add a focused host test or QEMU smoke test for every new failure path.
- Preserve the current C ABI. Version an ABI before changing its layout.
- Keep scheduling mechanisms in the kernel and service policy in user space.
- Do not begin compositor, window, widget, or desktop implementation here.
- Split modules when responsibilities diverge; do not split only to satisfy a
  line-count target.

## Out Of Scope

- SMP scheduling
- AArch64 implementation
- Dynamic linking and shared libraries
- General network or package services
- A production cryptographic trust chain
- Compositor and window policy

The design must leave room for these features, but Phase 3.5 does not implement
them.

## 3.5A. Baseline And Invariants

- [x] **A01: Record the pre-stabilization regression baseline**
  Run the complete Phase 3 closure matrix and archive a concise result.
  Completion: the starting revision and every test group are recorded.

- [x] **A02: Document scheduler and process state transitions**
  Define valid transitions for ready, running, sleeping, waiting, returned,
  failed, and reaped processes.
  Completion: every state has an owner, entry condition, and exit condition.

- [x] **A03: Document lock and interrupt-context rules**
  State which process, IPC, service, and memory operations may run with
  interrupts disabled or from IRQ context.
  Completion: prohibited blocking and allocation paths are explicit.

Phase 3.5A contracts:

- `docs/phase3_5_baseline.md`
- `docs/process_scheduler_invariants.md`
- `docs/kernel_context_rules.md`

## 3.5B. Common Wait And Wakeup Core

- [x] **W01: Add a kernel wait-reason contract**
  Represent sleep, child wait, IPC receive, and input wait with explicit wait
  reasons instead of unrelated flags.
  Completion: diagnostics report one unambiguous reason per blocked process.

- [x] **W02: Add common block and wake operations**
  Centralize transition to waiting and restoration to ready state.
  Completion: callers cannot directly assemble partial waiting state.

- [x] **W03: Convert timer sleep to the common wait core**
  Keep deadline ordering and wake each expired process exactly once.
  Completion: existing sleep and scheduler tests pass without special cases.

- [x] **W04: Convert IPC and input waits**
  Remove duplicated cooperative polling loops and route both through common
  blocking primitives.
  Completion: service programs no longer need a pacing sleep before blocking.

- [x] **W05: Add timeout and cancellation results**
  Support finite waits and deterministic cancellation during process exit or
  focus loss.
  Completion: timeout, wake, and cancel races each have automated coverage.

Phase 3.5B contracts:

- `ProcessWaitReason` now covers timer, child, IPC, input, key, and character waits.
- `process_wait_begin`, `process_wait_signal`, `process_wait_cancel`, and
  `process_wait_tick` are the only common wait-state transition helpers.
- Timer sleep, IPC receive, input-event wait, keyboard-event wait, and legacy
  `getchar` all pause user context and resume through the scheduler.
- SDK IPC/input timeout wrappers return stable `OS_ERR_TIMEOUT`; cancellation
  reports `OS_ERR_CANCELLED` where the wait owner is invalidated.
- `serviced_c.elf`, `inputd_c.elf`, and `displayd_c.elf` block directly on IPC
  and no longer need artificial pacing sleeps.

## 3.5C. Process Lifecycle Hardening

- [x] **P01: Centralize process termination**
  Route normal return, user fault, forced stop, and launch failure through one
  cleanup sequence.
  Completion: each path releases the same owned resources exactly once.

- [x] **P02: Strengthen process identity**
  Pair each reusable process slot with a generation value.
  Completion: stale process identities cannot target a replacement process.

- [x] **P03: Replace raw PID IPC targets with process identities**
  Preserve a compatibility wrapper for existing SDK programs while the new
  ABI is introduced.
  Completion: queued requests cannot cross a PID-generation boundary.

- [x] **P04: Stabilize parent, child, wait, and reap policy**
  Define orphan handling, bounded result retention, and forced child cleanup.
  Completion: repeated nested launches do not fill the process table.

- [x] **P05: Add process lifecycle stress coverage**
  Exercise normal exits, faults, background children, waits, and forced stops
  for at least 1,000 cycles.
  Completion: active slots and owned-resource counts return to baseline.

Phase 3.5C contracts:

- Each runtime process identity is now `(pid, generation)`.
- `process_mark_returned` and `process_mark_failed` route through one shared
  `process_finish` cleanup path.
- Process diagnostics print generation and parent generation.
- IPC receive records the sender generation in `OsIpcMessage.sender_generation`.
- Existing `os_msg_send(pid, ...)` remains as the compatibility path; new code
  can use `os_msg_send_to_identity`.
- Service replies and the `upong` IPC sample reply to the exact sender identity.
- Parent termination reaps terminal child results and orphans live children so
  stale parent identities do not attach to a replacement process.
- `make test-process-lifecycle` covers identity reuse, child wait wakeup,
  bounded result retention, orphaning, and 1,000 process-slot reuse cycles.

## 3.5D. Address Space And User Fault Isolation

- [x] **M01: Introduce an address-space object**
  Move user mappings, heap break, code, stack, and ownership metadata behind
  one process-owned object.
  Completion: user-pointer checks query the address-space interface.

- [x] **M02: Give each process an independent page-table root**
  Retain required kernel mappings while separating user mappings.
  Completion: one process cannot resolve another process's user pages.

- [x] **M03: Switch address spaces during scheduling**
  Update the architecture backend and preserve TLB correctness.
  Completion: yield, sleep, preemption, and nested process launch remain stable.

- [x] **M04: Add guarded user stacks**
  Leave at least one unmapped guard page adjacent to each user stack.
  Completion: stack overflow terminates only the offending process.

- [x] **M05: Enforce W^X and mapping ownership**
  Prevent writable and executable user mappings and reject foreign mappings in
  every syscall copy path.
  Completion: code, data, heap, and stack permissions are regression tested.

- [x] **M06: Make user faults recoverable**
  Convert user page fault and GP fault into process failure and scheduler
  continuation.
  Completion: the shell and unrelated services survive repeated user faults.

Phase 3.5D contracts:

- Each process owns an `AddressSpace` record with its page-table root, code,
  ELF link alias, guarded stack, heap, and bounded region metadata.
- x86_64 VM now supports creating, switching, and editing explicit page-table
  roots while retaining the kernel root as the fallback address space.
- User code, stack, and heap pages are mapped into the process root instead of
  directly into the kernel root.
- User execution switches CR3 to the process root and restores the parent or
  kernel root when returning from user mode.
- User stacks reserve one unmapped guard page below the usable stack range.
- ELF writable pages are NX; executable pages are not writable. Flat user
  programs are mapped read/execute after load.
- Syscall copy helpers validate buffers through the current process address
  space and page flags before dereferencing user pointers.
- User page faults and user general-protection faults mark only the current
  process failed and return control to the scheduler path.
- `make test-phase1` covers recoverable user page/GP faults, and
  `make test-user-sdk` covers heap, syscall pointer validation, yield, sleep,
  IPC wait, graphics, and input through the per-process address-space path.

## 3.5E. Kernel Objects And Handles

- [x] **H01: Define a typed kernel-handle ABI**
  Include object type, slot, generation, and rights without exposing pointers.
  Completion: malformed, stale, and wrong-type handles are rejected.

- [x] **H02: Add per-process handle tables**
  Track ownership and close all handles during centralized process cleanup.
  Completion: process exit leaves no live owned handles.

- [x] **H03: Move VFS handles behind the common handle layer**
  Preserve SDK file behavior while eliminating subsystem-specific ownership
  rules.
  Completion: file close, duplicate close, and exit cleanup are tested.

- [~] **H04: Add transferable shared-memory objects**
  Support bounded page-backed regions and explicit mapping rights.
  Completion: two processes can share data without sharing arbitrary memory.
  Current: handle type and rights are reserved; mapping and transfer syscalls
  are intentionally deferred to IPC v2.

- [~] **H05: Add graphics-surface handles**
  Define ownership, dimensions, format, stride, mapping, and destruction.
  Completion: a future compositor can receive a surface handle over IPC.
  Current: handle type and rights are reserved; compositor-facing allocation
  and IPC transfer come after IPC v2.

Phase 3.5E contracts:

- `KernelHandleTable` is embedded in each process and uses generation-checked
  opaque tokens.
- Handle tokens expose no kernel pointers and are valid only inside the owning
  process handle table.
- Current active object types are VFS file and VFS directory handles.
- `SYS_VFS_OPEN` and `SYS_VFS_OPENDIR` return typed handles, not raw VFS table
  indexes.
- VFS read, write, seek, tell, close, readdir, and closedir resolve type and
  rights before touching the VFS table.
- Process termination closes VFS-owned resources and invalidates all remaining
  process handles through the centralized cleanup path.
- `tools/kernel_handle_test.py` covers malformed, stale, wrong-type,
  insufficient-rights, duplicate-close, and table-full behavior.
- Detailed contract: `docs/kernel_handles.md`

## 3.5F. IPC v2

- [ ] **I01: Version the IPC ABI**
  Keep the Phase 3 fixed message format as v1 and define explicit v2 feature
  negotiation.
  Completion: v1 programs continue to run or fail with a clear version error.

- [ ] **I02: Add correlated request helpers**
  Standardize request ids, expected sender identity, timeout, and cancellation.
  Completion: unrelated messages cannot satisfy a pending request.

- [ ] **I03: Transfer handles through IPC**
  Validate sender rights and create receiver-owned handle entries atomically.
  Completion: failed delivery does not leak or partially transfer a handle.

- [ ] **I04: Define mailbox backpressure**
  Document nonblocking failure, bounded blocking send, timeout, and process-exit
  behavior.
  Completion: queue saturation cannot deadlock the system.

- [ ] **I05: Add IPC stress and fault injection**
  Cover wraparound, saturation, receiver exit, stale identity, timeout, and
  concurrent request/reply traffic.
  Completion: at least 100,000 local messages complete without leaks or hangs.

## 3.5G. Service Supervision And Permissions

- [ ] **S01: Formalize service states**
  Use stopped, starting, running, stopping, and failed transitions with a
  monotonic generation.
  Completion: registry and manager diagnostics agree on each state.

- [ ] **S02: Add start and stop timeouts**
  Detect services that never register or never exit.
  Completion: the manager remains responsive and reports a stable timeout.

- [ ] **S03: Add health checks and failure reporting**
  Define a bounded ping/status protocol and retain the last failure reason.
  Completion: diagnostics distinguish stopped, failed, and unresponsive.

- [ ] **S04: Add restart policy**
  Support disabled, on-failure, and bounded retry modes with backoff.
  Completion: a crash loop cannot consume all process slots or CPU time.

- [ ] **S05: Validate the dependency graph before launch**
  Reject missing dependencies and cycles without recursive runtime failure.
  Completion: dependency errors identify the involved services.

- [ ] **S06: Add service permission metadata v1**
  Start with static permissions for service discovery, IPC targets, input,
  display, and shared surfaces.
  Completion: a service without a permission receives a stable denial.

- [ ] **S07: Add service lifecycle stress coverage**
  Start, query, stop, crash, and restart services for at least 1,000 cycles.
  Completion: process, registry, mailbox, handle, and memory counts return to
  baseline.

## 3.5H. Concurrency Readiness

- [ ] **C01: Add interrupt-safe spinlock primitives**
  Define lock ordering and save/restore interrupt-state behavior.
  Completion: nested misuse is detectable in diagnostic builds.

- [ ] **C02: Protect process, IPC, service, and handle tables**
  Add the smallest lock boundaries that preserve each invariant.
  Completion: no code blocks, sleeps, or performs user copies while holding an
  inappropriate spinlock.

- [ ] **C03: Add counter and snapshot consistency**
  Make shell diagnostics observe coherent state without consuming queues.
  Completion: stress tests cannot produce impossible counts or partial records.

SMP remains out of scope. These tasks only prevent the current single-CPU
interrupt and scheduler paths from depending on accidental atomicity.

## 3.5I. Fault Injection And Soak Testing

- [ ] **T01: Add deterministic allocation-failure injection**
  Cover PMM, heap, process, mailbox, registry, handle, and shared-memory
  allocation failures.
  Completion: each failure unwinds without leaks or kernel panic.

- [ ] **T02: Add malformed syscall and IPC fuzz cases**
  Exercise invalid pointers, lengths, flags, handles, names, and ABI versions.
  Completion: only the offending request or process fails.

- [ ] **T03: Add a long-running service soak test**
  Run shell, service manager, input, display, IPC traffic, sleep, and process
  churn together.
  Completion: a one-hour QEMU run has no panic, hang, or monotonic leak.

- [ ] **T04: Add resource accounting diagnostics**
  Report active processes, mappings, PMM pages, heap use, mailboxes, services,
  handles, and shared surfaces.
  Completion: tests can compare a baseline snapshot with the final snapshot.

## 3.5J. Closure

- [ ] **Z01: Run all Phase 1 through Phase 3 regressions**
  Completion: every existing regression group passes from a clean build.

- [ ] **Z02: Run the new stabilization stress matrix**
  Completion: lifecycle, IPC, service, fault-injection, and soak tests pass.

- [ ] **Z03: Freeze ABI versions for Phase 4**
  Record process identity, handle, shared-memory, surface, IPC v2, and service
  protocol layouts.
  Completion: kernel and SDK compile-time assertions cover every shared type.

- [ ] **Z04: Publish the Phase 3.5 regression matrix**
  Map each invariant to an automated command and document residual risks.

## Phase 4 Entry Criteria

Phase 4 may begin when all of the following are true:

- User faults terminate only the offending process.
- Every process has an independent user address space.
- IPC and input waits use the common wait/wakeup core.
- Stale process and handle identities are rejected by generation checks.
- Large GUI payloads use shared-memory surface handles, not inline IPC copies.
- Service start, stop, crash, timeout, and restart are bounded.
- 1,000 process and service lifecycle cycles return all resources to baseline.
- 100,000 IPC messages complete without leaks, corruption, or deadlock.
- The one-hour QEMU soak test passes.
- The complete Phase 1 through Phase 3 regression baseline remains green.

## Required Test Baseline

After each numbered task, run the smallest relevant test plus:

```sh
make test-phase1
```

Before closing Phase 3.5, run:

```sh
make clean
make all
make uefi
make test-phase1
python3 tools/uefi_smoke.py
python3 tools/uefi_userland_smoke.py
python3 tools/uefi_screen_smoke.py
make test-user-sdk
make test-graphics
make test-input
make test-ipc
make test-services
```

The Phase 3.5 implementation must add its own process, address-space, handle,
IPC v2, service-supervision, fault-injection, and soak targets to this list.
