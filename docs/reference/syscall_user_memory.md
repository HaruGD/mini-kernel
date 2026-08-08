# System Call User-Memory Boundary

This document defines the kernel boundary for memory supplied by a user-mode
system-call caller. The per-argument policy source of truth is
`config/abi/syscalls.json`; the implementation is
`kernel/syscall/user_memory.cpp`.

## Address And Range Contract

User-memory operations accept only the current active process and lower-half
canonical x86_64 addresses below `0x0000800000000000`. A nonempty range must
have a nonzero start, satisfy its declared power-of-two alignment, end without
wrapping, belong entirely to the caller's recorded address-space regions, and
have a present user PTE for every covered page. Write or read/write ranges also
require region and PTE write permission.

Empty ranges succeed only when the caller passes `USER_MEMORY_ALLOW_EMPTY`.
They publish or consume no bytes and may use address zero. Unknown helper flags,
invalid alignments, a non-current process, or use without process context return
`OS_ERR_INVALID_ARGUMENT`.

Checked addition and multiplication clear their output before returning
`OS_ERR_OVERFLOW`. Noncanonical, null nonempty, unmapped, misaligned, and
permission-incompatible ranges return `OS_ERR_BAD_BUFFER`. A concurrent address
space mutation or poisoned TLB-shootdown state returns `OS_ERR_NOT_READY`; the
caller may retry the whole operation only if its syscall contract permits it.

## Lease And Unmap Exclusion

Validation acquires a short-lived `UserMemoryLease`. The address space counts
active leases, and every map, unmap, protection, region, and reset mutation
closes the mutation gate and waits for that count to reach zero before changing
the mapping. A copy therefore cannot pass validation and then fault because a
racing thread removed or downgraded its pages.

The lease stabilizes mapping identity, page coverage, and permissions. It does
not freeze byte contents: another thread in the same process can still write a
writable input buffer. Handlers must snapshot bounded request metadata into
kernel-owned storage before semantic validation, taking handles, or acquiring
subsystem locks. Nested pointers are never followed from live user memory: the
outer structure is copied first, then each nested count/range is checked and
copied separately.

No lease may begin while a kernel spinlock is held or while the CPU is in a TLB
acknowledgement wait. Copy helpers do not allocate, sleep, fault pages in, call
subsystems, or retain a lease across handler work. Mapping mutation waits occur
without the address-space spinlock held.

## Copy Operations

The common boundary provides:

- fixed input and output copies with explicit size, alignment, and empty-range
  policy;
- array input after checked `count * element_size` multiplication;
- bounded NUL-terminated byte strings that validate only pages reached before
  the terminator and return `OS_ERR_BUFFER_TOO_SMALL` when none is found;
- versioned structures whose leading little-endian `uint32_t size` and
  `uint32_t version` are snapshotted, checked, copied again as one bounded
  request, and rechecked before publication to kernel storage;
- strict UTF-8 validation for contracts that explicitly require UTF-8.

`cstring` arguments currently mean bounded NUL-terminated bytes, not implicit
UTF-8. A call requiring UTF-8 must say so in its owning protocol and invoke the
validator after copying.

All fixed input copies complete validation before modifying kernel destination
storage. A bounded string can contain an unusable prefix after a later-page or
missing-terminator failure; callers must discard that destination on any error.
Output copies validate the full destination before the first store, so a
validation failure leaves user output unchanged. Once validation succeeds, the
unmap lease makes the bounded store sequence nonfaulting. Atomic publication
means all bytes are written before a successful syscall result; it does not
promise hardware-atomic observation by another user thread.

## Machine-Readable Argument Policy

Catalog schema version 3 requires every argument to declare `alignment`, memory
`access`, snapshot/publication point, byte/string/structure encoding, and
whether nested buffers require snapshot-then-validate processing. The generated
kernel descriptor carries pointer, readable, writable, snapshot, nested, and
alignment metadata. Phase 5S-D consumes that metadata in the dispatcher.

Until the 5S-G per-call audit closes, catalog rows remain `provisional`, and an
alignment value of one records the compatibility behavior actually enforced by
legacy handlers.

## Verification

`make test-syscall-validation` covers checked arithmetic, canonical and
misaligned addresses, mapped cross-page copies, unmapped ranges, read-only
outputs, empty-range policy, bounded strings at page boundaries, versioned
structures, UTF-8, failure-state preservation, mutation-gate refusal, and a
real concurrent unmap held behind an active lease.

The User SDK QEMU test additionally exercises kernel and unmapped pointers,
wrapping ranges, a read-only output mapping, and a valid cross-page write through
the real dispatcher. Fault injection and the complete per-call validation and
permission matrix remain part of P5S-R03 and close in 5S-D/G, not in this
foundation alone.
