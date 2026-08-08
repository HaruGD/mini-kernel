# Kernel Language And Toolchain Contract

## Purpose And Scope

OS64 uses a deliberately restricted freestanding C++ implementation language
inside the kernel. This is not permission to use the hosted C++ application
runtime. The contract applies to active sources under `kernel/`, `drivers/`,
`arch/`, and `include/`, to linked kernel drivers, and to packaged C++ `.drv`
projects where a rule is explicitly shared.

Assembly remains responsible for CPU entry, interrupt and syscall frames, and
context transitions. C remains appropriate for firmware interfaces and plain
data boundaries. Every public syscall, user SDK, driver, boot, and assembly
boundary uses a documented C ABI; no public contract depends on C++ name
mangling, class layout, vtables, constructors, exceptions, or RTTI.

Language choice does not weaken the repository's documented-by-design rule.
Every public operation still requires explicit success semantics, a distinct
error code when the caller needs a failure cause, ownership and lifecycle
rules, concurrency rules, and regression evidence.

## Required Kernel Profile

The linked kernel is compiled as GNU C++17 with:

```text
-ffreestanding
-nostdlib
-nostartfiles
-nodefaultlibs
-std=gnu++17
-fno-exceptions
-fno-rtti
-fno-use-cxa-atexit
-fno-threadsafe-statics
-fno-unwind-tables
-fno-asynchronous-unwind-tables
```

Linked and packaged C++ drivers use the same restrictions. The kernel supplies
only its audited allocation operators and a fail-closed `__cxa_pure_virtual`
trap. There is no hosted standard library, exception unwinder, RTTI runtime,
thread-safe local-static guard runtime, or process-exit destructor service.

Allowed zero-cost language facilities include namespaces, non-virtual classes,
plain constructors called at a controlled boundary, `constexpr`, scoped enums,
templates with bounded instantiation, overloads, references, and compile-time
assertions. Their generated code and ownership behavior remain reviewable.

The following are forbidden in active kernel and linked-driver sources:

- `throw`, `try`, and `catch`;
- RTTI, `dynamic_cast`, and `typeid`;
- hosted `std::` library facilities;
- implicit dependence on exit-time destructors;
- function-local objects that require dynamic guard initialization;
- hidden allocation or blocking in a constructor or destructor;
- language objects in a public ABI.

## Initialization And Runtime Ownership

BSS is cleared before any C++ initialization. The architecture entry then
walks `.init_array` once on the BSP before `kernel64_main`. A global initializer
is allowed only when it establishes a fixed boot-lifetime hardware object that
cannot allocate, block, access VFS, enable interrupts, or depend on another
subsystem being initialized.

The current allowlist contains exactly two translation-unit initializers:

- `_GLOBAL__sub_I_terminal`, which constructs the boot-lifetime terminal,
  ATA, PS/2, PIT, and FAT32 objects in declared order;
- `_GLOBAL__sub_I_gop`, which constructs the zero-resource GOP state object.

All other subsystem initialization is explicit. A new global initializer is a
contract change and must document its order, context, failure behavior, and
reason it cannot be replaced by explicit initialization. Avoidable scalar
initialization must remain constant-initialized and must not enter
`.init_array`.

Kernel allocation operators are owned by `kernel/mm/heap.cpp`. No second C++
runtime implementation may exist. Ordinary `new` and `delete` are forbidden
before `heap_init`; placement construction is allowed on valid caller-owned
storage. Allocation failure follows the documented allocator or subsystem
error contract and must never become an exception.

## Dynamic Dispatch Policy

Virtual dispatch is limited to the legacy boot-lifetime `Driver::init`
boundary declared in `include/drivers/driver.h`. The current kernel vtable
allowlist is:

- `ATADriver`;
- `FAT32Driver`;
- `KeyboardDriver`;
- `PIT`;
- `Ps2MouseDriver`.

New kernel services, handles, filesystems, graphics backends, and driver ABI
objects use explicit C operation tables or direct calls unless a measured and
documented reason justifies changing this allowlist. Destruction through the
legacy base is not a runtime lifecycle mechanism; these objects live for the
boot lifetime.

## Optimization Profiles

`KERNEL_OPT` selects exactly one linked-kernel optimization level. The default
is the size-oriented profile:

```sh
make uefi                  # KERNEL_OPT=-Os
make KERNEL_OPT=-Og uefi  # debugging profile
make KERNEL_OPT=-O2 uefi  # throughput profile
```

Base C and C++ flags contain no competing optimization option. Profile changes
update a build stamp and rebuild every kernel object, so switching profiles
cannot silently reuse objects compiled under the previous profile. Unsupported
or multiple optimization values fail during Makefile evaluation. Profile
changes must pass the same ABI, binary-contract, QEMU, fault, and concurrency tests.
Performance claims require measured workloads; neither C nor C++ is assumed to
be faster from source spelling alone.

## Binary Budgets And Rejection Rules

The contract gate rejects unreviewed runtime growth. Current alert ceilings are:

| Region | Ceiling | Meaning |
| --- | ---: | --- |
| ELF text | 384 KiB | linked executable code and read-only implementation |
| ELF data | 64 KiB | writable initialized data |
| ELF BSS | 2 MiB | zero-initialized kernel static storage |
| `.init_array` | 16 bytes | exactly two allowed initializer pointers |

These are review budgets, not promises that memory is available. Raising one
requires measured current values, an ownership explanation, and regression
evidence. The gate also rejects:

- undefined kernel symbols;
- exception, unwind, RTTI, or local-static guard runtime symbols;
- `.eh_frame`, `.gcc_except_table`, or `.fini_array` sections;
- initializer or vtable names outside their allowlists;
- forbidden source constructs or duplicated runtime sources;
- build recipes that bypass `KERNEL_OPT`.

## Concurrency And Failure Rules

Constructors run with interrupts disabled on the BSP and may not acquire a
runtime lock, wait, allocate, call a driver, or access a partially initialized
CPU-local object. Runtime C++ helpers obey the same execution-context and lock
ordering rules as C functions. RAII does not authorize blocking cleanup or
hidden lock acquisition; every such wrapper must document its context and
generated cleanup behavior.

A pure virtual call is an unrecoverable kernel invariant violation and enters
the fail-closed halt trap. Bad user input, allocation failure, stale handles,
unsupported operations, and permission failures are ordinary subsystem errors
and must return their documented error codes instead of reaching a language
runtime trap.

## Verification And Change Procedure

Run:

```sh
make test-kernel-language-contract
```

The target builds `bin/kernel64.elf`, audits source and compiler policy, checks
an alternate `KERNEL_OPT=-O2` dry run, inspects symbols and sections, verifies
initializer and vtable allowlists, and records text/data/BSS measurements. It
is part of `test-closure`, so the current inherited closure cannot pass when
this contract drifts.

Any change to this policy updates this document, the automated allowlist or
budget, affected architecture/reference material, and the measured evidence in
the active phase record in the same work item.

## Current Measured Baseline

Measured on 2026-08-08 from clean builds:

| Profile | Text | Data | BSS | Initializers | Vtables | Result |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| default `-Os` | 215,202 bytes | 416 bytes | 1,630,752 bytes | 2 | 5 | Pass |
| throughput `-O2` | 280,124 bytes | 416 bytes | 1,630,752 bytes | 2 | 5 | Pass |

Both profiles linked without undefined symbols or forbidden runtime/exception
sections. The default UEFI image was rebuilt with `-Os` after the alternate
profile check.
