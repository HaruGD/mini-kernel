# Kernel Objects And Handles

Phase 3.5E introduces a typed per-process handle layer. User space receives
opaque integer tokens instead of subsystem pointers or raw kernel object
indexes.

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

The VFS file and directory types are active in the syscall path. Shared-memory
and graphics-surface types are reserved for IPC v2 and compositor work.

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
path invalidates the table after closing VFS-owned resources. This means:

- duplicate close fails;
- stale handles fail after close;
- a process cannot use another process's handle token;
- process exit leaves no live VFS handle reachable from user space.

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
