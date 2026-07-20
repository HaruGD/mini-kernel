# Phase 4 Implementation Plan

This document turns the frozen Phase 4 compositor and window-service
contracts into an executable implementation sequence. Each subphase must leave
the tree buildable, add focused regression coverage, and satisfy its exit gate
before the next subphase begins.

Live implementation state and completed evidence are recorded in
`docs/phases/phase-4/progress.md`. Contract-to-test coverage is recorded in
`docs/phases/phase-4/regression_matrix.md`. Do not rewrite this plan to make
partial implementation appear complete.

## Target Architecture

Phase 4 keeps GUI policy in supervised user-space services. All three services
run in Ring 3, but they are system components managed by `serviced_c.elf`, not
ordinary interactive applications.

```text
GUI applications
    | window requests, damage, and transferred surface handles
    v
windowd_c.elf
    | window server + compositor v1
    | final composite surface and accepted damage
    v
displayd_c.elf
    | sole normal DISPLAY authority
    v
GOP display backend / future display driver

input driver -> kernel input queue -> inputd_c.elf -> windowd_c.elf
                                               -> focused GUI application
```

`windowd` combines the window server and compositor for the first version, but
its window policy, composition, protocol, and input-routing code remain
separate internal modules. A separate compositor process is not a Phase 4
goal.

The Phase 4 service model is:

- `inputd`: supervised system service with `INPUT` permission;
- `displayd`: supervised system service with `DISPLAY` permission;
- `windowd`: supervised system service without raw `INPUT` or `DISPLAY`;
- GUI applications: unprivileged clients with IPC, discovery, and shared
  surface permissions only.

User accounts, login sessions, per-user service managers, and moving `windowd`
into a user session are later work. Phase 4 must not pretend that the current
single-system identity model already provides those boundaries.

## Existing Gaps To Close First

The following are implementation blockers, not optional cleanup:

1. Graphics-surface pixels are currently allocated from the kernel heap.
   Heap pages must never be mapped directly into a user process.
2. Processes do not yet record general object mappings, so surface unmap and
   abnormal-exit cleanup cannot be made reliable without new bookkeeping.
3. IPC handle transfer currently clones source rights. A client surface must
   arrive at `windowd` as `READ | MAP`, never with client write authority.
4. IPC v2 has a 96-byte payload. Logical damage submissions therefore need a
   bounded chunk protocol rather than oversized messages.
5. Ordinary launches currently retain all permissions for compatibility.
   Phase 4 cannot claim exclusive display and input ownership until normal GUI
   applications receive a least-privilege launch profile.

## Cross-Phase Rules

- Every object count, page count, byte calculation, rectangle list, message
  sequence, and retry loop is bounded before state mutation.
- User mappings are NX and become writable only when the process-local handle
  has `WRITE` rights.
- An operation either completes or rolls back all handles, mappings, pages,
  registry state, and references created by that operation.
- PID is never sufficient for ownership; use PID plus process generation.
- Interrupts remain enabled during pixel copy, composition, and framebuffer
  writes. IRQ-off sections cover ownership state transitions only.
- Services block on IPC/input or a bounded frame deadline. Busy polling is
  forbidden.
- Each subphase adds host-side ABI/unit tests and at least one QEMU integration
  path appropriate to the new boundary.

## Phase 4A: Page-Backed Surface Foundation

Goal: replace heap-backed surface storage with a kernel object that can be
safely mapped into user address spaces.

Implementation:

- move surface-specific storage and mapping logic out of the generic handle
  implementation into the kernel graphics domain;
- allocate zero-filled PMM pages and retain heap allocation only for bounded
  object metadata such as a physical-page list;
- provide a contiguous kernel virtual view of non-contiguous physical pages so
  existing software drawing and blit code can use linear pixels;
- reserve a non-overlapping user virtual mapping arena for shared objects;
- add a bounded per-process surface-mapping table containing object identity,
  user address, byte/page count, flags, and mapping generation;
- unmap process page-table entries without freeing object pages while another
  handle reference exists;
- release the kernel view and PMM pages only when the final object reference is
  dropped;
- enforce dimension, multiplication, format, object, per-process mapping, and
  global page-budget limits before allocation;
- preserve kernel-internal graphics users while changing the backing model.

Required tests:

- zero-filled allocation and RGB/BGR layout;
- minimum, maximum, invalid, and overflowing dimensions;
- partial PMM or mapping failure rollback;
- repeated create/release without page or object drift;
- existing kernel graphics and screen smoke tests.

Exit gate: internal and handle-backed surfaces use page-backed storage, all
failure paths are leak-free, and no user mapping API has exposed kernel-heap
memory.

## Phase 4B: Surface ABI, Mapping, And Transfer Rights

Goal: expose safe process-local surface access and safe cross-process transfer.

Implementation:

- add the frozen create, get-info, map, unmap, and close syscalls;
- add matching public types and User SDK wrappers;
- return application handles with `WRITE | MAP | TRANSFER` as required by the
  contract;
- add rights-attenuating handle transfer: requested receiver rights must be a
  subset of source rights and client surfaces must reach `windowd` as
  `READ | MAP`;
- strip or explicitly control re-transfer authority instead of copying it by
  accident;
- reject map protection that exceeds handle rights;
- make repeated unmap/close, wrong-address unmap, stale handle, wrong object
  type, and cross-process handle use fail safely;
- walk the per-process mapping table during normal exit, kill, and recoverable
  user-fault cleanup;
- introduce least-privilege application launch profiles while keeping
  privileged diagnostic programs explicit.

Required tests:

- writable application mapping and read-only compositor mapping;
- denied rights escalation and denied write through a read-only clone;
- transferred-object survival after sender exit;
- final-reference cleanup after receiver exit;
- transfer and mapping rollback under injected handle, region, and page-table
  exhaustion;
- syscall ABI layout and SDK integration tests.

Exit gate: one process can create and draw into a surface, transfer a read-only
view to another process, exit in either order, and leave no handle, mapping, or
page leak.

## Phase 4C: Display Service Present Path

Goal: make `displayd` the sole normal user-space path to the physical display.

Implementation:

- define a versioned display-present protocol with frame generation,
  correlation id, format, dimensions, and bounded damage;
- use a begin/chunk/commit transaction, or an equivalent bounded transaction,
  because all damage rectangles cannot fit in one IPC v2 payload;
- transfer one read-only composite-surface handle at frame begin and validate
  every following chunk against the same sender and frame generation;
- process only one present transaction at a time and acknowledge the exact
  accepted generation;
- clip damage again at the display boundary and collapse overflow to the newest
  full-screen frame;
- introduce a display backend interface with the current GOP path as the
  fallback implementation and a stable insertion point for a future hardware
  display driver;
- restrict direct normal display syscalls to `displayd`;
- retain kernel terminal output for boot, panic, and GUI-service failure;
- add `displayd` to the supervised startup path with its explicit permission
  profile and restart policy.

Required tests:

- full-frame and partial-damage present;
- stale, duplicated, missing, reordered, and oversized chunks;
- invalid handle type, rights, dimensions, format, and sender identity;
- display ownership under concurrent terminal and service activity;
- `displayd` crash/restart with terminal fallback still usable.

Exit gate: a test producer can transfer a surface to `displayd`, receive an
acknowledgement for the accepted generation, and ordinary applications cannot
present directly.

## Phase 4D: Single-Window Bring-Up

Goal: prove the complete application-to-screen path before adding general
window policy.

Implementation:

- add `windowd_c.elf` as a supervised system service registered as `window`;
- split its source internally into protocol, window-state, compositor, and
  input-router modules even though they remain one process;
- allocate one display-sized composite surface;
- support one client, one full-screen window, one attached client surface, and
  full-screen damage;
- compose by opaque copy into the composite surface and submit it to
  `displayd`;
- keep damage pending until `displayd` acknowledges the frame generation;
- block on IPC with a bounded frame deadline and coalesce repeated client
  updates;
- reconnect to a restarted `displayd` and resubmit a full frame.

Required tests:

- `CREATE`, `SET_SURFACE`, full damage, and `DESTROY` happy path;
- wrong owner, stale generation, malformed ABI, and unexpected client exit;
- a QEMU demo that draws a deterministic test pattern through
  `application -> windowd -> displayd`, with no direct display syscall.

Exit gate: the first full-screen GUI client is visible exclusively through the
service pipeline and all three services remain supervised.

## Phase 4E: Multiwindow Compositor

Goal: extend the proven pipeline to bounded overlapping windows and partial
composition.

Implementation:

- add a fixed-capacity window table with generation-checked id, owner identity,
  geometry, visibility, z-order, surface, and damage state;
- support at most 12 windows and one current client surface per window;
- implement `CREATE`, `DESTROY`, `SET_SURFACE`, `DAMAGE`, `SHOW`, and `HIDE`
  before adding `MOVE` and `RESIZE`;
- validate damage in surface coordinates, clip it, translate it into screen
  coordinates, and merge it into the screen-damage accumulator;
- chunk a logical client damage submission across IPC messages using a
  submission id, ordered chunk index, declared total, and final marker;
- accept at most 16 rectangles per logical window submission and 64 rectangles
  in the screen accumulator; collapse accumulator overflow to full screen;
- composite the background and visible windows in stable z-order using opaque
  RGB/BGR copies only;
- atomically replace a window surface after successful validation and retain
  the prior valid state on failure.

Required tests:

- overlapping windows, clipping on every screen edge, hide/show, and z-order;
- moved and resized surfaces, replacement rollback, and destroyed owners;
- malformed and incomplete chunk transactions;
- rectangle arithmetic overflow and full-screen overflow collapse;
- deterministic composed-frame hashes or pixel probes in host and QEMU tests.

Exit gate: multiple overlapping windows update only accepted damage, compose
deterministically, and survive arbitrary client destruction order without
resource drift.

## Phase 4F: Input Routing And Focus

Goal: route normalized keyboard input to exactly one valid window owner.

Implementation:

- make `inputd` block on the kernel input queue and forward normalized events
  to `windowd` over a versioned IPC protocol;
- keep raw `INPUT` permission exclusively in `inputd`;
- let `windowd`, not `inputd`, own focus policy;
- track either one keyboard-focused window or no focused window;
- emit ordered `FOCUS_OUT` then `FOCUS_IN` events with window id, generation,
  event sequence, and original input timestamp;
- send events to owner PID plus generation and reject stale recipients;
- clear or move focus when a window is hidden, destroyed, replaced by a stale
  identity, or becomes noninteractive;
- begin with PS/2 keyboard input; pointer hardware, pointer capture, and global
  shortcuts remain non-blocking follow-up work.

Required tests:

- keyboard delivery to one focused client and no delivery to background
  clients;
- focus ordering and sequence monotonicity;
- focus cleanup on hide, destroy, crash, and PID reuse;
- bounded input overflow and unresponsive-client behavior;
- `inputd` and `windowd` restart/reconnection paths.

Exit gate: keyboard input crosses the complete driver-to-application path and
never reaches a hidden, destroyed, unfocused, or stale-generation window.

## Phase 4G: Window SDK And First GUI Application

Goal: provide the stable client-facing API and demonstrate a real event-driven
GUI program.

Implementation:

- add shared `window_types.h` ABI definitions and mirrored SDK headers;
- add surface drawing helpers that operate on mapped pixel buffers instead of
  direct display syscalls;
- add window create/destroy, attach/replace surface, damage, move, resize,
  show/hide, focus, information, and event-wait SDK operations;
- hide chunked transport inside the SDK so applications submit ordinary
  rectangle arrays;
- create the first GUI demo with a mapped surface, software drawing, damage
  submission, keyboard event loop, resize handling, and clean shutdown;
- keep widgets, desktop shell, decorations, fonts beyond the existing text
  foundation, and alpha effects outside the required Phase 4 client API.

Required tests:

- public ABI size/offset checks and SDK negative cases;
- SDK request/reply correlation and unexpected-message handling;
- GUI demo launch, draw, key event, redraw, resize, and exit in QEMU;
- proof that the GUI demo has neither raw `INPUT` nor `DISPLAY` permission.

Exit gate: a normal application uses only the public SDK to open a window,
draw, receive input, redraw damaged regions, resize, and close cleanly.

## Phase 4H: Lifecycle, Fault, Regression, And Closure

Status: Complete on 2026-07-20 at implementation commits `31b681e` and
`8d44929`; reproducible evidence is recorded in `progress.md`.

Goal: certify the GUI stack under malformed input, resource pressure, process
failure, and repeated lifecycle operations.

The console/GUI ownership state is defined in
`docs/architecture/console_gui_handoff.md`. The scheduler boundary and the
post-Phase-4 threading/SMP sequence are defined in
`docs/architecture/scheduler_modernization.md`.

Implementation and tests:

- add a persistent generation-tagged console/GUI display session so terminal
  rendering cannot overwrite pixels between GUI presents;
- capture a read-only retained console underlay on first-window acquisition,
  compose normal windows above it, keep GUI-time logs in serial/klog and the
  retained terminal state, and redraw the current console on last-window
  release;
- transfer input exactly once with visible display mode, reject stale session
  owners, and restore console input on normal release or GUI-service failure;
- make the single-CPU scheduler select ready user processes while the kernel
  shell is idle, with an explicit idle task or equivalent idle state;
- correct foreground IPC wait handling so `service` and
  `service status window` run `serviced`, resume the exact waiting client, and
  restore the shell only after one terminal process result; add a bounded
  `usvcctl` reply timeout as a secondary failure guard;
- remove the foreground `drive`/`udrive_c.elf` scheduler helper from the shell,
  GUI/input tests, and normal application execution after drive-free QEMU
  scheduling tests pass;
- inject PMM, mapping-region, handle-table, IPC-queue, and service-launch
  exhaustion at every allocation boundary;
- crash clients during create, transfer, damage submission, composition,
  present, resize, and shutdown;
- restart `displayd`, then restore `windowd` connectivity and require clients
  to reconnect or recreate protocol state explicitly;
- verify fallback terminal usability when either GUI service exceeds its
  bounded restart policy;
- repeat window/surface create, map, transfer, replace, unmap, destroy, and
  process-exit cycles while sampling process, handle, page, region, object, and
  service-registry counts;
- add a 60-second event, damage, composition, present, service-restart, and
  client-churn soak with zero unexplained drift;
- run the full closure suite, clean parallel build, UEFI image smoke, and QEMU
  GUI smoke from a clean tree;
- update the roadmap, regression matrix, project history, and Phase 4 closure
  record with measured results.

The one-hour soak remains an explicit release-certification run rather than a
mandatory inner development test unless the release checklist requests it.

Exit gate: terminal output cannot alter GUI scanout while GUI mode is active;
the retained console appears below normal windows and is restored with command
input after last-window close or GUI failure; services and GUI applications
continue running while the kernel shell is idle with no `drive`-style
foreground helper; all focused and full regression suites pass; the 60-second
soak has no unexplained resource drift; the normal display/input permissions
are exclusive; and the Phase 4 closure record contains reproducible evidence.

## Dependency And Commit Sequence

```text
4A page-backed storage
 -> 4B user mapping and safe transfer
 -> 4C exclusive display service
 -> 4D one full-screen window
 -> 4E bounded multiwindow composition
 -> 4F keyboard focus routing
 -> 4G public SDK and first GUI app
 -> 4H console/GUI handoff, drive-free scheduling, fault, soak, and closure
```

Each subphase receives an implementation/test commit followed by an evidence
commit that records the already-known implementation hash and measured test
results. No later step may bypass an earlier service boundary for convenience.
In particular, the first GUI demo must not write the framebuffer directly, and
`windowd` must not acquire `DISPLAY` permission merely to simplify bring-up.

## Deferred Work

The following are intentionally outside Phase 4:

- a compositor process separate from `windowd`;
- user accounts, login sessions, and a per-user service manager;
- multiple simultaneous graphical sessions and virtual-terminal switching;
- alpha blending, shadows, animation policy, and GPU acceleration;
- pointer capture, global shortcuts, input methods, and clipboard services;
- desktop shell, panel, notifications, full widgets, and application toolkit;
- remote display and network-transparent window protocols.

These features must build on the Phase 4 surface, window, focus, and service
contracts instead of weakening them.
