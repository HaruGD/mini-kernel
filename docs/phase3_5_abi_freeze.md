# Phase 3.5 ABI Freeze

This document freezes the shared layouts used at the Phase 4 boundary. A
layout change requires a new ABI version; existing version constants must not
be silently redefined.

## Frozen Versions And Layouts

| ABI | Version | Frozen records |
| --- | ---: | --- |
| Process | 1 | `OsProcessIdentity` (8 bytes) |
| Handle/object | 1 | `OsHandle` (8 bytes), `OsSharedMemoryInfo` (24 bytes), `OsGraphicsSurfaceHandleInfo` (32 bytes) |
| IPC | v1=1, v2=2 | `OsIpcMessage` (88 bytes), `OsIpcMessageV2` (152 bytes), `OsIpcReceiveFilter` (24 bytes) |
| Service registry | 1 | `OsServiceInfo` (36 bytes) |
| Service protocol | 2 | query (16 bytes), input/health reply (32 bytes), display reply (48 bytes) |
| Service manager | 2 | request (32 bytes), reply (64 bytes) |
| Graphics | 1 | point, rectangle, surface info, graphics info |
| Input | 1 | key, pointer, and aggregate input events |

All sizes and every externally visible field offset are asserted in the
headers under `include/os64`. SDK forwarding headers include those exact files
instead of maintaining copies.

## Handle Token Encoding

`OsHandle` is a 64-bit syscall value. ABI v1 currently accepts values in the
low 32 bits:

- bits 0..7: table slot plus one;
- bits 8..31: 24-bit generation;
- token zero and slot byte zero are invalid.

The encoding constants live in `os64/handle_types.h`; kernel handle code uses
the same constants. Handle object IDs remain kernel-private and are not part
of this ABI.

## Compatibility Rule

Adding a field, changing a field type or offset, changing token encoding, or
changing accepted flags requires an ABI version increment and an explicit
compatibility path. New enum values that old peers can safely reject may be
added without changing record layout, but protocol behavior must still be
documented.

`make test-abi-freeze` compiles every shared header as C11 and C++17 through
both the kernel include root and the public SDK include root. Header-level
static assertions are therefore enforced from both consumers.
