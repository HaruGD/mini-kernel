# Phase 3 Task Breakdown

Phase 3 adds the first IPC and user-space service foundation. The goal is not
to build the compositor or desktop yet. The goal is to let long-running user
programs register services and exchange bounded messages without hardwiring
future GUI policy into the kernel.

## Working Rules

- Complete one numbered task per commit unless two tasks are inseparable.
- Every task must keep `make test-phase1` passing.
- Add the smallest relevant test with the implementation.
- Keep the kernel IPC layer as a transport and bookkeeping mechanism, not a
  service policy engine.
- Services are ordinary ELF user programs. Drivers remain `.drv` packages.
- Keep service manager policy in user space unless the kernel must enforce a
  safety invariant.
- Split files by responsibility when a module gains a second distinct job.
- Do not start compositor, window-manager, or desktop policy during this phase.

## 3A. IPC Contracts

- [x] **M01: Define the IPC message ABI**
  Add fixed-size message headers, sender pid, type, flags, payload length, and
  a small inline payload.
  Completion: kernel/user ABI size assertions compile.

- [x] **M02: Define IPC result codes and limits**
  Add stable errors for no target, queue full, bad buffer, message too large,
  permission denied, and would-block.
  Completion: SDK result strings and kernel errors match.

- [x] **M03: Add process mailbox fields**
  Add a bounded per-process IPC queue separate from the input-event queue.
  Completion: process creation, clear, return, failure, and reap paths reset
  mailbox state.

- [x] **M04: Add mailbox unit tests**
  Test empty/full state, FIFO order, wraparound, bad arguments, and overflow
  policy.
  Completion: host-side mailbox tests pass without booting QEMU.

## 3B. IPC Syscalls

- [x] **M05: Add nonblocking send syscall**
  Copy a user message into the target process mailbox.
  Completion: invalid pid, self-send, full queue, and bad user pointers are
  covered.

- [x] **M06: Add nonblocking receive syscall**
  Copy one queued message from the current process mailbox to user space.
  Completion: empty queue returns a stable would-block error.

- [x] **M07: Add blocking receive syscall**
  Put the current process to sleep until a message arrives, without busy
  waiting.
  Completion: injected message wakes exactly one waiting process.

- [x] **M08: Add reply helper semantics**
  Standardize request/reply message types and sender pid handling.
  Completion: a user program can send a request and receive a reply from a
  child or sibling process.

- [x] **M09: Clean up IPC on process exit**
  Drop queued messages to dead processes and wake receivers with a stable
  not-ready result.
  Completion: exiting senders/receivers do not leave stale waiters.

## 3C. IPC SDK And Diagnostics

- [x] **M10: Add SDK IPC API**
  Add `os_msg_send`, `os_msg_recv`, `os_msg_wait`, and small helper types under
  `<os64/os64.h>`.
  Completion: SDK callers do not use raw syscall numbers.

- [x] **M11: Add IPC diagnostics**
  Add kernel shell inspection for per-process mailbox depth and dropped/woken
  counters.
  Completion: diagnostics do not consume queued messages.

- [x] **M12: Add IPC sample programs**
  Add `uping_c.elf` and `upong_c.elf` or equivalent request/reply examples.
  Completion: QEMU smoke confirms round-trip message delivery.

- [x] **M13: Add `make test-ipc`**
  Combine host mailbox tests and QEMU IPC smoke into one target.
  Completion: `make test-ipc` passes from a clean build.

## 3D. Service Registry

- [x] **S01: Define service identity ABI**
  Use short fixed names such as `input`, `display`, and `window`; service files
  remain normal `.elf` binaries.
  Completion: invalid names and duplicate names are rejected.

- [x] **S02: Add kernel service registry table**
  Track service name, owner pid, state, flags, and generation.
  Completion: process exit unregisters owned services.

- [x] **S03: Add service register/find/unregister syscalls**
  Allow a service process to publish itself and clients to find its pid.
  Completion: stale pids are never returned.

- [x] **S04: Add SDK service API**
  Add `os_service_register`, `os_service_find`, and
  `os_service_unregister`.
  Completion: user programs can discover services without kernel-shell help.

- [x] **S05: Add service diagnostics**
  Add `services` shell command to list names, pids, states, and flags.
  Completion: diagnostics survive service exit and restart.

## 3E. Service Manager v1

- [ ] **S06: Add `serviced.elf` skeleton**
  Build the first user-space service manager as a normal ELF program.
  Completion: it starts, registers as `service`, waits for IPC, and exits on a
  test command.

- [ ] **S07: Add service start policy**
  Let `serviced.elf` start known service binaries by name through existing
  process launch mechanisms.
  Completion: starting an already running service is idempotent.

- [ ] **S08: Add service stop/restart policy**
  Stop and restart owned child services without leaking process table entries.
  Completion: stopped services disappear from lookup and can be restarted.

- [ ] **S09: Add dependency metadata v1**
  Use a small static table first; defer package manifests until the behavior is
  proven.
  Completion: dependent services start after their prerequisites.

- [ ] **S10: Add service manager shell commands**
  Add `service start`, `service stop`, `service restart`, and `services` front
  ends.
  Completion: commands route through IPC where possible.

## 3F. First User-Space Services

- [ ] **V01: Add `inputd.elf` placeholder**
  Register as `input` and expose a minimal request/reply API over IPC.
  Completion: clients can query input service status.

- [ ] **V02: Add `displayd.elf` placeholder**
  Register as `display` and expose graphics/display capability info over IPC.
  Completion: clients can query current display dimensions.

- [ ] **V03: Add service client sample**
  Add a small user program that discovers `input` and `display` and sends test
  requests.
  Completion: the sample reports success/failure without manual inspection.

- [ ] **V04: Add service smoke test**
  Boot QEMU, start `serviced.elf`, start placeholder services, run the client,
  and verify the serial log.
  Completion: service startup and IPC request/reply are automated.

## 3G. Phase Closure

- [ ] **T01: Run the Phase 3 regression baseline**
  Run Phase 1, UEFI, userland, SDK, graphics, input, IPC, and service tests.

- [ ] **T02: Add a Phase 3 regression matrix**
  Document IPC, service registry, service manager, and placeholder service
  coverage.

- [ ] **T03: Update README, roadmap, and SDK docs**
  Close Phase 3 documentation and point Phase 4 at compositor/window work.

## Required Test Baseline

After every task:

```sh
make test-phase1
```

Before marking Phase 3 complete:

```sh
make clean && make all && make uefi
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
