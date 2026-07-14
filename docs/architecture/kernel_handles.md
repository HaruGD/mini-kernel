# Kernel Objects And Handles

Phase 3.5E introduces a typed per-process handle and object layer. User space
receives opaque integer tokens instead of subsystem pointers or raw kernel
object indexes.

## Handle Token

A handle token is currently a 32-bit value carried in the 64-bit syscall ABI:

```text
bits 0..7    slot + 1
bits 8..31   generation
```

Slot zero is encoded as `1`, so token value `0` remains invalid. Generation
checks reject stale handles after close and slot reuse.

## Object Types

- `VFS_FILE`
- `VFS_DIR`
- `SHARED_MEMORY`
- `GRAPHICS_SURFACE`

VFS file and directory handles wrap existing VFS table indexes. Shared-memory
and graphics-surface handles own refcounted kernel objects managed by
`kernel/handle/kernel_objects.cpp`.

## Rights

- `READ`
- `WRITE`
- `SEEK`
- `ENUMERATE`
- `MAP`
- `TRANSFER`

Syscalls must resolve the handle with the expected type and rights before
touching a subsystem object. Wrong-type, stale, malformed, and insufficient
rights handles fail before reaching the subsystem.

## Process Ownership

Each `Process` owns a `KernelHandleTable`. The centralized process termination
path invalidates the table after closing VFS-owned resources and releasing
refcounted objects. This means:

- duplicate close fails;
- stale handles fail after close;
- a process cannot use another process's handle token;
- process exit leaves no live owned handle reachable from user space;
- shared-memory and graphics-surface objects are destroyed after their final
  handle reference is closed.

## Refcounted Objects

`kernel_shared_memory_create()` allocates a bounded page-backed byte region and
returns a handle with caller-selected rights. The object records owner PID,
requested size, allocated page count, rights, generation, and refcount.

`kernel_graphics_surface_create()` allocates a bounded 32-bit pixel surface and
returns a handle that can be passed to future compositor or graphics service
code. Surface pixels are zero-filled PMM pages, not kernel-heap storage.
Non-contiguous physical pages are mapped into a fixed per-object kernel virtual
slot so the software renderer retains a linear pixel view. The object records
owner PID, dimensions, stride, pixel format, logical byte size, backing page
count, generation, and refcount. Closing the final reference unmaps the kernel
view and returns every backing page.

Both object families use generation-checked object IDs internally, so stale
handle entries cannot resurrect destroyed object slots.

## Handle Transfer

IPC v2 transfers handles by resolving a sender handle with the `TRANSFER`
right, retaining the underlying object, and allocating a receiver-owned handle
token. If any transfer in a send fails, already cloned receiver handles are
rolled back and their object references are released.

Only refcounted kernel object handles are cloneable through
`kernel_object_clone_handle()`. VFS file and directory handles are intentionally
not cloneable through IPC, even if a future bug accidentally gives them
`TRANSFER` rights.

## VFS Compatibility

The VFS still keeps its internal open-file table. The syscall layer now wraps
raw VFS file and directory indexes in per-process kernel handles:

```text
os_open()
  -> SYS_VFS_OPEN
  -> vfs_open_for_owner()
  -> kernel_handle_alloc(VFS_FILE)
  -> opaque handle returned to user space
```

Read, write, seek, tell, close, readdir, and closedir resolve the opaque handle
back to the raw VFS index only after type and rights validation.

Kernel and driver-internal VFS calls may continue using raw VFS indexes until a
future driver ABI revision moves them behind capability handles.
