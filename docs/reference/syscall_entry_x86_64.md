# x86_64 System Call Entry Contract

This document defines the Phase 5S-E architecture boundary implemented by
`arch/x86_64/syscall64.asm`, `kernel/syscall/entry64.cpp`, and the shared
dispatcher in `kernel/syscall/dispatcher.cpp`. It does not change syscall
numbers, arguments, results, permissions, or user-memory policy.

## Per-CPU Initialization

Every BSP or AP must have a valid CPU identity and aligned current kernel
entry stack before it can become syscall-ready. Initialization verifies the
extended CPUID `SYSCALL/SYSRET` bit, then programs and reads back:

- `IA32_EFER.SCE`;
- `IA32_STAR` with kernel `CS=0x08`, kernel `SS=0x10`, user `CS=0x2b`, and
  user `SS=0x23`;
- `IA32_LSTAR` with `syscall_entry64`;
- `IA32_FMASK=0x44700`, masking TF, IF, DF, NT, and AC on entry.

An AP that cannot establish this state does not transition to `ONLINE`.
`gdt64_set_kernel_stack` updates the TSS `RSP0` and the fast-entry stack
together whenever the scheduler selects a different thread stack.

Fast-entry state occupies one page-aligned, fixed-stride record per logical
CPU. The assembly-visible prefix has compile-time offset and size assertions;
process, thread, and address-space objects remain private to C++ code. The
existing `CpuLocal` layout is not extended or reordered.

## Entry And Common Frame

User mode cannot write GS base: FSGSBASE remains disabled, user TLS uses FS,
and both kernel GS MSRs identify the current CPU under the Phase 4.6 policy.
The kernel writes a per-CPU user-execution marker immediately before `IRETQ`.
`SYSCALL` first preserves user `R10` in the established CPU-local scratch,
derives the fixed per-CPU entry record from the immutable GS logical ID,
checks that marker, executes `SWAPGS`, records the untrusted user `RSP`, and
switches to the current thread's aligned kernel stack before any push.

The entry then synthesizes the same logical 20-word frame used by the DPL3
`int 0x80` gate:

```text
r15..r8, rdi, rsi, rbp, rdx, rcx, rbx, rax,
user RIP, user CS, user RFLAGS, user RSP, user SS
```

`RCX` supplies the return RIP and `R11` supplies the return flags. The saved
copy of `R10` replaces the temporary entry value before C code runs. Entry
clears DF, leaves interrupts disabled until the complete frame exists, and
calls the same `syscall_dispatch64` used by `int 0x80`. Blocking `yield`,
`sleep`, and wait results reuse the existing saved-frame functions; no second
dispatcher or result domain exists.

## Return Validation

Before a normal fast return, the kernel verifies all of the following again:

- the captured process and thread pointer plus both generation identities;
- active, non-exiting lifecycle and exact thread ownership;
- the currently loaded address-space pointer, identity, and root;
- user selectors `CS=0x2b` and `SS=0x23`;
- nonzero lower-canonical RIP and RSP;
- executable ownership of RIP and writable ownership of `RSP-1`;
- a sanitized user RFLAGS value with bit 1 forced set.

Safe states return with `SYSRETQ`. TF, DF, RF, AC, unsupported flag bits, or
any state unsuitable for the fast instruction use the checked `IRETQ`
fallback. An invalid selector, address, mapping, identity, or address space
terminates only the calling process as `PROCESS_TERM_GP_FAULT` with diagnostic
status `0x5e01`; it never attempts `SYSRETQ` and never panics the kernel.

Blocking and process-exit control tokens discard the transient fast-entry
record before returning to the scheduler. Resume uses the existing complete
thread frame and rewrites the user marker on the CPU that actually resumes the
thread, so migration does not retain a stale entry record.

## ABI And Compatibility

The raw register ABI remains `RAX` for number/result and `RDI`, `RSI`, `RDX`
for the current three arguments. The `SYSCALL` instruction always clobbers
`RCX` and `R11`; inline assembly must also declare `memory`, and `cc` when it
modifies flags. Ordinary applications must continue to use SDK wrappers.

Phase 5S-E enables and tests the fast transport, but the checked-in SDK still
defaults to `int 0x80`. Phase 5S-F owns the default-transport switch, complete
in-tree rebuild, and the bounded retention or deliberate retirement policy
for `int 0x80`.

## Diagnostics And Verification

The `syscalls` shell command reports ready/known CPUs, fast entries,
`SYSRETQ`, `IRETQ`, abort, invariant-failure counts, and FMASK without user
payloads or addresses. The focused host test checks state transitions, return
validation, identity/address-space mutation, and source-level entry contracts.
The User SDK QEMU test mixes both transports and covers normal calls, explicit
errors, yield/sleep resume, forced DF fallback, and a noncanonical stack.

Run:

```sh
make test-syscall-entry
python3 tools/run_usdk_test.py --cpus 1
python3 tools/run_usdk_test.py --cpus 4
```
