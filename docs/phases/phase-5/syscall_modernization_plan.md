# Phase 5S: System Call Modernization

## Status And Placement

Phase 5S is a planned mandatory gate between the completed Phase 5D GUI
terminal and Phase 5E desktop shell. It modernizes the system-call contract and
x86_64 entry mechanism before the desktop adds more long-lived clients and
before Phase 5F adds directory, metadata, and file-mutation operations.

```text
5D GUI terminal (Complete)
  -> 5S system-call modernization (Planned, next)
  -> 5E desktop shell
  -> memory scalability gate
  -> 5F file manager
```

The memory-scalability gate may progress independently, but neither it nor
desktop work substitutes for the Phase 5S exit evidence.

## Current Baseline

The active x86_64 user SDK currently enters the kernel through the DPL3 IDT
vector `int 0x80`. The syscall number is in `RAX`, the first three arguments
are in `RDI`, `RSI`, and `RDX`, and the result returns in `RAX`. Active numbers
1 through 111 and kernel-side error constants are manually declared in
`include/kernel/syscall64.h`; the user SDK carries a separate result-code view.
The SDK correctly hides the raw entry instruction from ordinary applications,
but there is no machine-readable source that proves the number, argument,
pointer, permission, blocking, output, and error contracts remain synchronized.

The current interface already has useful subsystem-specific checks and stable
negative result values. Phase 5S preserves working behavior while replacing
manual duplication and filling contract gaps. It is not permission to renumber
working calls or silently change existing application behavior.

## Goals

- make one machine-readable syscall catalog the source of truth;
- generate shared numbers, kernel descriptors, SDK bindings, reference tables,
  and drift checks from that catalog;
- give every operation explicit success, output-state, and distinct error
  semantics without ambiguous failure `NULL`, zero, or boolean values;
- centralize checked user-range, string, structure, and copy rules;
- enforce permission, ownership, lifecycle, cancellation, cleanup, execution
  context, blocking, and lock rules at one reviewable boundary;
- make x86_64 `SYSCALL/SYSRET` the default fast entry path without creating a
  second semantic dispatcher;
- preserve or deliberately retire `int 0x80` through a tested compatibility
  policy;
- prove malformed and hostile calls fail locally on one and four CPUs without
  kernel corruption, deadlock, collateral process loss, or resource drift.

## Non-Goals

Phase 5S does not add Linux/POSIX binary compatibility, promise that OS64
syscall numbers match another operating system, redesign every subsystem, or
introduce Phase 5F file-manager operations early. It does not treat
`SYSCALL/SYSRET` as a security or performance result by itself. Performance
claims require measurements, and semantic correctness takes precedence over a
fast return path.

## 5S-A: Catalog, Schema, And Generated ABI

Add a versioned catalog, planned at `config/abi/syscalls.json`, with a schema
that rejects duplicate numbers, names, invalid types, missing limits, unknown
permissions, and incomplete result contracts. Each entry records at least:

- stable number, symbolic name, ABI version, lifecycle state, and compatibility
  policy;
- each argument's register, width, signedness, direction, type, alignment, and
  relationship to any byte/count argument;
- scalar bounds and pointer address-space, access, nullability, length,
  structure-version, and nested-pointer rules;
- required process permission, handle type and rights, object ownership, and
  generation checks;
- allowed execution context, whether the operation can block, timeout and
  cancellation behavior, and restart policy;
- success value, output validity on success and failure, partial-result rules,
  and the complete allowed error set;
- resource limits, cleanup responsibility, and an owning reference document.

Generation must be deterministic. Checked-in generated artifacts are rejected
when stale. The catalog generates shared number constants for the kernel and
SDK, kernel dispatch metadata, SDK internal wrappers where practical, a public
result-code table, reference documentation, and compile-time ABI assertions.
Generated files never replace the reviewed subsystem implementation.

Removing or reusing a published number is forbidden. New calls append a new
number or introduce a documented versioned operation. Reserved and retired
entries remain in the catalog so their numbers cannot be recycled.

## 5S-B: Result And Output Contract

Use one stable signed result domain shared by the kernel and SDK. Zero may mean
success only when the catalog says the operation has no positive result.
Positive byte counts, identifiers, and other values remain operation-specific
success results. Every recoverable failure is a documented negative code.

The audit must distinguish at least invalid scalar arguments, invalid or
inaccessible user memory, unsupported operation/version, permission denial,
stale or wrong-type handles, absent objects, resource exhaustion, memory
exhaustion, overflow/range failure, would-block, timeout, cancellation, I/O
failure, and queue/capacity failure. Additional filesystem and lifecycle errors
are added only with stable meanings and generated SDK strings.

Every output pointer or structure has an exact failure-state rule. The default
is no published output on failure. Operations that can publish a partial byte
or entry count must identify which errors permit partial progress and how the
caller resumes. Internal scheduler control tokens never enter the public result
domain.

## 5S-C: User-Memory Validation And Copy Boundary

Provide common helpers for checked arithmetic, canonical user addresses,
range overflow, page coverage, mapping ownership, read/write permission,
alignment, bounded strings, fixed/versioned structures, arrays, and nested
buffers. Each call declares whether it snapshots input before acting and
whether output publication is atomic or intentionally partial.

Validation followed by direct use is not sufficient for mutable user memory.
Handlers copy bounded request metadata into kernel-owned storage before taking
subsystem locks. Copy faults return the documented error, release every
temporary reference, and do not panic the kernel. No handler faults user pages
in, blocks, or waits for another CPU while holding a spinlock or a lock needed
by page-fault, process-exit, unmap, or TLB-shootdown paths.

Unknown syscall numbers, extra flag bits, future structure versions, invalid
UTF-8 where required, zero/overflow lengths, boundary-spanning buffers,
read-only outputs, unmapped holes, kernel addresses, and mappings removed by a
racing thread all have deterministic fail-closed results.

Implementation status (2026-08-08): the schema-v3 catalog metadata, common
checked-copy API, mapping lease, address-space mutation exclusion, and focused
host/QEMU validation cases are implemented. P5S-R03 remains in progress because
descriptor permission preflight, injected copy/allocation faults, and the
complete per-call semantic audit belong to 5S-D/G.

## 5S-D: Dispatch, Permissions, And Auditability

Both entry transports call one descriptor-driven dispatcher. The dispatcher
performs number lookup, lifecycle/version checks, execution-context checks,
generic scalar and pointer validation, and permission preflight before calling
the subsystem handler. Subsystem validation remains responsible for semantic
state and object invariants.

The catalog names the authority for every privileged call. Handle-consuming
operations validate type, rights, owner generation, and transfer policy.
Failures expose no kernel pointer, physical address, stale object content, or
data belonging to another process. Diagnostics count rejected calls by stable
reason without logging unbounded user payloads or secrets.

## 5S-E: x86_64 `SYSCALL/SYSRET` Entry

On every online CPU, initialization verifies architectural support and programs
`IA32_EFER.SCE`, `IA32_STAR`, `IA32_LSTAR`, and `IA32_FMASK` with documented
selectors and flags. AP bring-up cannot publish a CPU as syscall-ready before
its CPU-local entry state and kernel entry stack are valid.

The assembly entry path must:

- use `SWAPGS` only under a proven user-entry state and reach valid CPU-local
  storage before dereferencing it;
- capture the untrusted user `RSP` in CPU-local entry scratch, switch to the
  current thread's bounded kernel stack without pushing to the user stack, and
  then materialize the complete saved frame including return `RIP` from `RCX`,
  flags from `R11`, and every ABI-required register;
- clear direction state, mask the documented flags, maintain the kernel ABI's
  stack alignment, and avoid enabling interrupts before the complete frame is
  recoverable;
- construct the same logical saved context used by scheduling, sleep, waits,
  cancellation, process exit, signal/fault handling, and preemption;
- validate the return `RIP`, `RSP`, selectors, flags, process generation, and
  address space after any blocking or migration event;
- use `SYSRETQ` only for a safe canonical fast return and use a checked `IRETQ`
  fallback for states that `SYSRETQ` cannot return safely.

`SYSCALL` clobbers `RCX` and `R11`; the SDK clobber contract must state this.
The default ABI continues to use `RAX` for the number/result and `RDI`, `RSI`,
and `RDX` for the current three arguments unless a separately versioned call
requires more. No handler depends on C++ name mangling or an assembly-visible
C++ object layout.

## 5S-F: Compatibility Migration

First route `int 0x80` and `SYSCALL` through the same dispatcher and result
contract. Rebuild every in-tree program against the new SDK, make `SYSCALL` the
default, and test mixed old/new entry calls. The legacy vector is then either:

- retained as a documented compatibility entry with identical validation and
  bounded support; or
- disabled for the release only after the built image contains no legacy call
  site and a legacy attempt returns or faults according to a documented test.

There is never a second syscall-number namespace or a legacy path that bypasses
permission and user-memory checks. Compatibility decisions are recorded per
catalog entry and in the release history.

## 5S-G: Complete Call Audit

Audit every existing number, not just newly used desktop calls. For each call,
resolve mismatches among the dispatcher, kernel header, public SDK, reference
documents, tests, and actual output/error behavior. Split calls that conflate
unrelated operations, replace silent truncation, and document deliberately
retained compatibility behavior.

The audit covers process/thread lifecycle, waits and timeouts, VFS, heap,
graphics, input, IPC, services, handles, display sessions, surfaces,
synchronization, affinity, and terminal sessions. A call is not marked audited
until its positive, negative, permission, pointer, cleanup, and blocking tests
exist.

## 5S-H: Regression, Fault Injection, And Closure

Planned focused targets are:

```text
make test-syscall-contract
make test-syscall-entry
make test-syscall-validation
make test-syscall-faults
make test-syscall-soak
make test-phase5s
```

Host checks cover schema validation, deterministic generation, stale artifacts,
duplicate/reserved numbers, C/C++ header views, SDK clobbers, allowed errors,
documentation links, and dispatcher coverage. QEMU checks cover both entry
transports during migration, one and four CPUs, blocking and preemption,
cross-page and racing unmap buffers, noncanonical addresses, bad return state,
unknown numbers, every permission class, process exit during a call, allocation
failure at each boundary, and exact cleanup.

A repeatable syscall stress workload mixes valid traffic with deterministic
malformed inputs and fault injection. It records call/rejection counters,
process/thread/handle state, blocked waiters, PMM and heap use, lock violations,
and warmed/final resources. The optional one-hour soak is not required for this
gate.

## Exit Gate

Phase 5S is complete only when:

- every active and reserved syscall number exists in the versioned catalog;
- all generated headers, SDK bindings, descriptors, references, and ABI checks
  are current and reproducible;
- every active call has explicit arguments, success/output, errors, permissions,
  ownership, cleanup, context, blocking, timeout, and cancellation rules;
- the default in-tree SDK uses the verified `SYSCALL/SYSRET` entry, with the
  `int 0x80` compatibility decision tested and documented;
- common validation prevents invalid user memory and permission bypass without
  panics, deadlocks, unrelated process loss, or partial output outside contract;
- the complete per-call audit, focused host/QEMU tests, one/four-CPU stress, and
  all affected inherited regression pass with measured evidence;
- the progress ledger names the implementation commits and real measurements.

Only then may Phase 5E begin. Phase 5F remains additionally gated by the PMM
memory-scalability work.
