# System Call Dispatch And Authority Contract

This document defines the common kernel boundary between an architecture entry
transport and an OS64 system-call handler. The machine-readable authority is
`config/abi/syscalls.json` schema/catalog version 4; the generated descriptor
view is `include/kernel/syscall_catalog_generated.h`; and the implementation is
`kernel/syscall/dispatcher.cpp`.

The active SDK still defaults to x86_64 `int 0x80`. Phase 5S-E added the
verified `SYSCALL/SYSRET` transport and Phase 5S-F will migrate the default;
both transports call the same
`syscall_dispatch64` function. A transport may construct and restore machine
state; it may not perform a second number lookup, permission policy, pointer
policy, or subsystem dispatch.

## Dispatch Order And Results

The dispatcher performs the following fail-closed sequence before invoking the
selected handler:

1. Look up the exact active descriptor. Zero, gaps, and numbers above the
   catalog return `OS_ERR_UNSUPPORTED`.
2. Require a nonzero current process identity in an active, non-exiting
   `RUNNING` or compatibility-summary `PAUSED` state. `PAUSED` is permitted
   because another thread may change the process summary while the current
   owned thread is executing on this CPU. Terminal states return
   `OS_ERR_NOT_READY`.
3. Require an active, non-exited current thread whose owner pointer, process
   identifier, and process generation all match. Failure returns
   `OS_ERR_NOT_READY`.
4. Reject process permission state containing bits outside
   `OS_PROCESS_PERMISSION_VALID_MASK` with `OS_ERR_PERMISSION_DENIED`.
5. Apply the descriptor authority preflight. Missing required permission bits
   return `OS_ERR_PERMISSION_DENIED` before any user pointer is inspected.
6. For non-partial output operations, check declared pointer nullability,
   range-addition overflow, canonical form, power-of-two alignment, and the
   required read/write access to the first byte. Overflow returns
   `OS_ERR_OVERFLOW`; all other pointer-shape/access failures return
   `OS_ERR_BAD_BUFFER`; a concurrent address-space mutation returns
   `OS_ERR_NOT_READY`.
7. Invoke the owning core, SDK, or VFS handler. The handler performs full-range
   copies and subsystem semantic validation.

A pointer whose catalog size is `argN bytes` has its end arithmetic checked by
the dispatcher. A zero-byte operation consumes or publishes no memory and may
use a null buffer. Bounded strings, fixed/versioned structures, nested data,
complete page coverage, scalar ranges, flags, and object invariants remain the
owning handler's responsibility through the common user-memory helpers. Their
per-call audit is Phase 5S-G; silent scalar truncation is not sanctioned by this
common preflight.

`SYS_IPC_QUERY` is the catalog's intentional partial-output operation. The
dispatcher does not reorder its pointers ahead of the handler because doing so
would change its documented publication order. Its handler validates and
publishes each output in contract order. All atomic-output calls complete the
common preflight before their handler can publish output.

## Authority Modes

Every call resolves one of four generated modes:

| Mode | Common preflight | Handler responsibility |
| --- | --- | --- |
| `none` | No permission bit is required. | Validate ordinary semantic state. |
| `permissions` | Require every listed process permission bit. | Validate object and operation semantics. |
| `subsystem` | No generic bit completely represents the authority. | Validate registered owner, relationship, or subsystem generation. |
| `permissions_then_subsystem` | Require every listed bit first. | Then validate the narrower owner/session/relationship authority. |

Permission preflight is an early rejection boundary, not a replacement for
defense in depth. Existing handler checks remain valid. Handle-consuming calls
must still use the common handle resolver or an equivalently exact subsystem
resolver to validate token format, generation, type, rights, owner generation,
and transfer policy. Failed resolution publishes no handle snapshot or object
content.

## Diagnostics And Privacy

The dispatcher maintains atomic totals for entered, dispatched, and rejected
calls plus one counter for each stable rejection reason. The reason set is:

`unknown_number`, `no_process_context`, `process_lifecycle`,
`no_thread_context`, `thread_owner`, `permission_state`, `permission_denied`,
`null_pointer`, `pointer_format`, `pointer_access`, and `execution_context`.

The kernel shell command `syscalls` prints the totals, the last rejected number
and reason, and nonzero reason counters. Diagnostics never record syscall
payload bytes, strings, user addresses, object contents, or secrets. Counters
are advisory observability data: relaxed atomic increments may race in display
order, but totals never require a dispatcher lock, allocation, or blocking.

## Concurrency, Cleanup, And Limits

Descriptor lookup is read-only and allocation-free. Permission and lifecycle
checks take no subsystem lock. The temporary pointer lease used by preflight is
released before handler entry; no user-memory lease crosses allocation,
blocking, subsystem work, or output publication. The stricter lease/unmap and
lock-context contract is defined in
[System Call User-Memory Boundary](syscall_user_memory.md).

The dispatcher supports the catalog's current three-register argument limit.
Adding a wider call requires a versioned catalog/entry contract rather than
reading undeclared registers. Unknown descriptors fail locally and never reach
a handler. Handler-owned temporary resources must be released on every result
path according to the syscall catalog and subsystem reference.

## Verification

- `make test-syscall-contract` rejects stale generation and invalid authority
  modes or permission symbols.
- `make test-syscall-validation` exercises lookup, lifecycle/thread ownership,
  corrupt permission state, permission-before-pointer ordering, pointer shape,
  range overflow, partial-output ordering, diagnostics, and handler routing.
- `python3 tools/run_usdk_test.py --cpus 1` and `--cpus 4` launch a process with
  zero permissions and verify every current permission class is rejected while
  unprivileged identity/time calls remain usable.
- IPC, surface, service, process, display/window, and GUI terminal inherited
  suites protect subsystem semantics and cleanup.
