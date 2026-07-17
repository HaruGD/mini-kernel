# Phase 4 Compositor And Window Service Contracts

This document freezes the Phase 4 boundaries before implementation. Phase 4
adds user-space GUI policy above the stabilized kernel, IPC, service, handle,
graphics, input, and driver foundations. It must not move window policy or
composition policy into the kernel.

## Process Topology

The first implementation uses three supervised system services:

```text
applications
    | window protocol + transferred surface handles
    v
windowd_c.elf
    | window manager + compositor v1
    | final composite surface + damage
    v
displayd_c.elf
    | sole normal display-present authority
    v
GOP/display driver

input driver -> kernel input queue -> inputd_c.elf -> windowd_c.elf -> focused application
```

`windowd_c.elf` combines window management and composition for v1. Splitting
them into separate processes is deferred until the protocol and performance
data justify the extra failure and synchronization boundary.

Responsibilities:

- `windowd`: window ownership, geometry, z-order, focus, damage aggregation,
  composition, and application event routing;
- `displayd`: display discovery, exclusive normal present authority, final
  surface validation, and GOP/display-driver submission;
- `inputd`: kernel input consumption, normalization, and forwarding to
  `windowd`; it never chooses application focus;
- kernel: bounded objects, mappings, handle rights, IPC transport, process
  cleanup, permissions, and final display mechanism only.

## Surface Contract

A window has exactly one current client surface in v1. A graphics surface is a
refcounted kernel object referenced by opaque process-local handles.

Required SDK operations:

```c
OsHandle os_surface_create(uint32_t width, uint32_t height, uint32_t format);
long os_surface_get_info(OsHandle surface, OsGraphicsSurfaceHandleInfo* info);
void* os_surface_map(OsHandle surface, uint32_t map_flags);
long os_surface_unmap(OsHandle surface, void* address);
long os_surface_close(OsHandle surface);
```

Required behavior:

- allocations are page-backed and zero-filled;
- dimensions, multiplication, byte size, object count, and mapped-page totals
  are bounded before allocation;
- application handles require `WRITE | MAP | TRANSFER`;
- compositor clones require `READ | MAP` and must not gain write access to a
  client surface;
- mappings are user, NX, and writable only when the handle has `WRITE`;
- closing or process exit unmaps the process view and releases its reference;
- the object survives while any transferred handle remains;
- no user process receives the physical framebuffer address;
- v1 supports RGB/BGR 32-bit opaque pixels only; alpha composition is deferred.

The current heap-backed internal surface allocation must become page-backed
before `os_surface_map()` is exposed. Mapping arbitrary kernel-heap pages into
user space is forbidden.

## Window Protocol v1

The protocol uses IPC v2, process identities, correlation ids, and at most two
transferred handles. New shared ABI types belong in
`include/os64/window_types.h` and the User SDK mirror.

Commands:

- `CREATE`: create a window with initial geometry and one transferred surface;
- `DESTROY`: destroy a caller-owned window;
- `SET_SURFACE`: atomically replace the client surface;
- `DAMAGE`: submit one or more changed rectangles;
- `MOVE`: request a new position;
- `RESIZE`: negotiate new dimensions and surface replacement;
- `SHOW` / `HIDE`: change visibility;
- `FOCUS`: explicit focus request subject to window-server policy;
- `GET_INFO`: return current geometry, state, and generation.

Events:

- `CREATED`, `CLOSED`, `MOVED`, `RESIZED`, `FOCUS_IN`, `FOCUS_OUT`;
- `KEY`, `POINTER`, and future text-input events;
- every event carries window id, window generation, and monotonically
  increasing event sequence.

Window ids are generation checked. A request is accepted only when sender
identity owns the target window, except for privileged window-service control.
Client exit destroys all owned windows and releases transferred handles.

## Composition And Damage

The v1 compositor uses a single display-sized composite surface.

Frame procedure:

1. drain window/input/control IPC without blocking the frame indefinitely;
2. validate and clip submitted damage to each client surface;
3. translate damage into screen coordinates;
4. merge into a bounded screen-damage list;
5. composite visible windows in z-order into the composite surface;
6. send the composite handle and bounded damage to `displayd`;
7. clear only damage accepted for presentation.

Bounds:

- maximum windows: 12;
- maximum surfaces remain constrained by the kernel object limit;
- maximum damage rectangles per window submission: 16;
- maximum accumulated screen damage rectangles: 64;
- overflow collapses to one full-screen rectangle;
- invalid or overflowing rectangles are rejected or safely clipped;
- v1 composition is opaque copy only.

The compositor must yield or block on IPC between frames. Busy polling is
forbidden.

## Display Ownership And Present

`displayd` is the only normal user-space process with `DISPLAY` permission.
Applications and `windowd` do not call GOP presentation directly.

The per-operation display-owner token is not sufficient for a graphical
session: terminal output can still draw between GUI presents. Phase 4H adds the
persistent, generation-tagged console/GUI session handoff defined in
`docs/architecture/console_gui_handoff.md`. While GUI mode is active, ordinary
terminal output updates retained console state and logs but never GOP scanout;
the retained console snapshot is composed below normal windows until the last
window closes or failure restores console mode.

Display ownership state transitions may briefly disable interrupts, but pixel
copy, composition, dirty present, and MMIO framebuffer writes must run with
interrupts enabled. An interrupt-side terminal draw that encounters another
owner skips that draw; it does not spin or extend IRQ latency.

Presentation policy:

- one present request is processed at a time;
- stale composite generations are dropped;
- damage is clipped again at the display boundary;
- queue overflow keeps the newest full-frame state rather than blocking IPC;
- terminal output remains the boot, panic, and compositor-failure fallback;
- service-manager restart restores `displayd`, then `windowd`, then client
  reconnection; the kernel never trusts a dead owner token.

## Input And Focus

`inputd` owns the kernel-focused input stream and forwards normalized events to
`windowd`. Only `windowd` decides which window receives them.

Rules:

- exactly one keyboard-focused window or no focused window;
- focus changes emit ordered `FOCUS_OUT` then `FOCUS_IN` events;
- input events include original timestamp and a window-server sequence;
- events are delivered using owner PID plus generation, never PID alone;
- hidden, destroyed, stale-generation, or noninteractive windows cannot hold
  focus;
- queue overflow uses the existing bounded drop accounting and never blocks an
  IRQ path;
- pointer capture and global shortcuts are deferred until explicitly designed.

PS/2 keyboard is sufficient for the first window/event-loop milestone. Mouse
hardware support is a parallel track and must not block keyboard-driven GUI
bring-up.

## Permissions

- applications: IPC, service discovery, and shared-surface creation/transfer;
- `windowd`: IPC, service registration/discovery, shared surfaces, input-event
  reception from `inputd`, and child cleanup as required;
- `displayd`: IPC, service registration, shared-surface read/map, and DISPLAY;
- `inputd`: IPC, service registration, and INPUT;
- no ordinary application receives DISPLAY or raw INPUT permission.

Every request validates ABI size/version, flags, sender identity, handle type,
rights, object generation, dimensions, and rectangle bounds before mutation.

## Failure And Cleanup

- application exit: destroy its windows, drop focus, release surfaces;
- application stall: retain last valid surface; do not stall composition;
- malformed request: reject only that request and preserve service state;
- `windowd` crash: supervisor restarts it, `displayd` retains/falls back to the
  last valid frame, and stale client handles are released by process cleanup;
- `displayd` crash: supervisor restarts it and terminal fallback remains usable;
- surface allocation/map failure: return bounded error without partial handle
  or mapping leaks;
- display present failure: retain damage for retry and expose diagnostics;
- service restart limit exhaustion: remain in terminal fallback and report the
  failure through service diagnostics.

## Phase 4 Implementation Order

The detailed subphase plan, required tests, and exit gates are recorded in
`docs/phases/phase-4/implementation_plan.md`.

1. Page-backed user surface syscall and SDK contract.
2. Surface handle transfer/mapping integration tests.
3. `displayd` present protocol and bounded ownership handoff.
4. `windowd` service with one full-screen surface.
5. Multiwindow z-order and damage composition.
6. `inputd` forwarding and keyboard focus routing.
7. Window protocol SDK and first GUI application.
8. Persistent console/GUI display and input handoff with exact fallback.
9. Drive-free idle-shell scheduling and lifecycle/failure regression matrix.

Each step adds focused host tests plus a QEMU integration test. Phase 4 does
not proceed to widgets or desktop components until window lifecycle, surface
cleanup, focus routing, and service restart paths pass soak coverage.
