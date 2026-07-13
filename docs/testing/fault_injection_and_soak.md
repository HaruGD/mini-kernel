# Fault Injection And Soak Testing

Phase 3.5I adds deterministic failure testing, malformed-request coverage, and
resource-baselined QEMU soak runs. Fault injection can only be armed from a
diagnostic boot image.

## Deterministic Failure Points

The kernel exposes seven one-shot points: `pmm`, `heap`, `process`, `mailbox`,
`service`, `handle`, and `shared`. `faultinject <point> N` allows N successful
admission attempts and fails the following attempt. The point disarms after
the injected failure. `faultinject off` clears all points and counters.

PMM and heap failures update their normal allocator failure counters. The
other points fail before publishing a process slot, queue record, registry
entry, handle, or shared-memory object. Shared-memory handle failure continues
to release the allocated object through its normal unwind path.

`faulttest` runs only on the diagnostic image. It verifies immediate PMM,
heap, process, mailbox, handle, and shared-memory failures and confirms that
the PMM free count, heap use, and shared-object accounting remain unchanged.
Host tests additionally cover service admission and fail-after-N semantics.

## Malformed Request Coverage

The IPC contract test covers invalid v1/v2 sizes, ABI versions, lengths,
message types, flags, filter fields, handle counts, and transferred handles.
The user SDK integration test submits invalid kernel-range pointers and
malformed service, VFS, and IPC requests through the real syscall boundary.
Every case must return a bounded error while the test process continues.

## Resource Snapshot

The `resources` shell command reports:

- active processes and address-space regions;
- handles, queued mailbox records, and registered services;
- shared-memory objects and graphics surfaces with byte totals;
- PMM free pages and failure count;
- heap used/mapped bytes and failure count.

The soak runner warms every process/service path before recording its baseline
because per-slot address-space roots and page tables are intentionally cached
for reuse. After warmup, the final snapshot must exactly match the baseline.

## Soak Commands

`make test-soak` runs the 60-second qualification. It keeps the input, display,
and service-manager processes active while repeatedly launching health clients
that exercise process creation, IPC request/reply, waits, sleeps, and cleanup.
Every tenth cycle restarts the display service. It rejects resource drift,
kernel panic output, and lock-order violations.

`make test-soak-hour` runs the same workload for 3,600 seconds. This is the
release-gate command for the one-hour Phase 3.5I requirement.

The Phase 3.5I certification run completed 2,049 health IPC cycles with
periodic display restarts. Its final resource snapshot matched the warmed
baseline exactly, all lock misuse counters remained zero, and no panic, hang,
or command timeout was observed.

The focused commands are:

```sh
make test-fault-injection
make test-user-sdk
make test-soak
make test-soak-hour
```
