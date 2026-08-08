# System Call Result And Output Contract

This document defines the public result domain shared by the OS64 kernel and
User SDK. The machine-readable source of truth is
`config/abi/syscalls.json`; the generated per-call table is
[syscall_catalog.generated.md](syscall_catalog.generated.md).

## Result Domain

`OsResult` is a signed 64-bit value returned in `RAX`.

- `0..INT64_MAX` is the success domain.
- `-4095..-1` is reserved for public, cataloged errors.
- A caller must interpret success according to the call's generated
  `success_domain`, not assume that every successful call returns zero.
- Unknown syscall numbers return `OS_ERR_UNSUPPORTED`.
- An uncataloged negative value is not a supported ABI result. The SDK reports
  it as `unknown error`, and contract tests reject raw numeric negative returns
  in syscall dispatch code.

The success domains are:

| Domain | Meaning |
| --- | --- |
| `zero` | The only returned success value is zero. |
| `nonnegative` | Zero or a positive count/value is successful. |
| `positive` | A positive handle or address is successful; zero is not returned as success. |
| `noreturn` | The caller exits; no public result reaches that caller. |

Internal scheduler tokens such as return-to-kernel, yield, sleep, and wait are
dispatcher control flow, not public errors. They are encoded outside the
public error range and must be consumed before user-mode return.

## Error Rules

Every recoverable public failure uses a generated `OS_ERR_*` value. Existing
values `-1..-18` remain stable. Phase 5S-B appends:

- `OS_ERR_INVALID_HANDLE`: malformed token or invalid encoded slot;
- `OS_ERR_STALE_HANDLE`: inactive slot or generation mismatch;
- `OS_ERR_WRONG_HANDLE_TYPE`: live handle of an unacceptable object type;
- `OS_ERR_OVERFLOW`: checked size, count, offset, or address arithmetic failed.

Missing named objects remain `OS_ERR_NOT_FOUND`; missing handle rights remain
`OS_ERR_PERMISSION_DENIED`. `retryable` metadata means that a corrected state
or later attempt can succeed. It never authorizes the SDK to retry
automatically, and it does not weaken timeout, cancellation, or idempotency
rules in the owning subsystem contract.

The common handle resolver publishes exact errors without modifying its
output snapshot on failure. VFS handle operations and surface/general object
handle inspection use that result directly. Remaining provisional call sites
are audited in Phase 5S-G before they may be marked `audited`.

## Output Publication

Every call has one generated output policy:

- `none`: the call has no output pointer;
- `atomic`: all declared output becomes valid only with a successful result;
  on failure the caller's output storage is unchanged;
- `partial`: only the errors listed in `partial_errors` can leave named output
  published, and the caller follows the generated `resume` rule.

The default for an output pointer is `atomic`, `unchanged`, and
`retry_entire_call`. Phase 5S-B records one current exception:
`SYS_IPC_QUERY` has two optional output pointers and copies the ABI version
before feature bits. `OS_ERR_BAD_BUFFER` on the second pointer can therefore
leave the first output published; the caller retries the entire call with
both output ranges valid.

A returned byte or entry count describes only successful publication. A
negative result never encodes a partial count. `SYS_VFS_READ` and
`SYS_VFS_READDIR` stage output in kernel storage and publish it only after the
operation succeeds; their caller buffers remain unchanged on failure.

## SDK Use

Applications normally call typed SDK wrappers. `os_result_failed()` accepts
the signed return value, and `os_result_string()` maps every cataloged error
through the generated result table. Any nonnegative operation-specific value
maps to `success`; an uncataloged negative value maps to `unknown error`.

The generated constants are an ABI building block, not permission to bypass
typed wrappers or the ownership, user-memory, and blocking contracts of the
owning subsystem.

## Concurrency And Failure State

Output state is defined at syscall completion. A handler must not publish
output and subsequently return an error unless its catalog entry names a
`partial` policy for that exact error. Temporary kernel allocations and object
references are released before an error returns. A handle can become stale
between calls; generation mismatch is reported locally and does not affect a
new object occupying the same slot.

Phase 5S-C will centralize checked user-memory copy mechanics. Phase 5S-G will
perform the complete positive, negative, permission, pointer, cleanup, and
blocking audit before changing each call's `audit_status` from `provisional`.
