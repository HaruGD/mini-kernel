# Window Service And Multiwindow ABI

Phase 4D introduced `windowd_c.elf` between GUI applications and
`displayd_c.elf`. Phase 4E extended the same supervised service into a bounded
opaque multiwindow compositor, and Phase 4F added normalized keyboard routing
and server-owned focus without granting `windowd` direct display or raw-input
authority.

```text
restricted GUI applications
  <- focused window events from windowd
  -> transferred client surfaces (READ | MAP at the receiver)
  -> windowd: ownership, geometry, z-order, damage, opaque composition
  -> one retained windowd composite surface
  -> displayd present protocol
  -> display backend / GOP
```

`windowd` is supervised as the system service `window`, depends on `display`,
and uses the GUI-service permission profile. It has no `DISPLAY` permission
and contains no direct graphics-present call. `displayd` remains the sole
normal presentation authority. The registered `inputd` identity is the sole
normal raw-input authority; it forwards normalized events to `windowd`, while
ordinary GUI clients have neither `INPUT` nor `DISPLAY` permission.

## Protocol v1

The shared ABI is `OS64_WINDOW_ABI_VERSION == 1` in
`include/os64/window_types.h`. The original Phase 4D request layouts remain
accepted. Phase 4E adds bounded geometry and partial-damage messages without
changing those layouts.

Commands:

- `CREATE`: transfers one client surface; the extended layout also supplies
  signed screen position and initial geometry;
- `SET_SURFACE`: atomically replaces a same-sized current surface;
- `DAMAGE`: retains the compatible full-window damage operation;
- `DAMAGE_BEGIN`, `DAMAGE_RECTS`, `DAMAGE_COMMIT`: submit 1-16 rectangles as
  an ordered transaction with at most four rectangles per 96-byte IPC chunk;
- `SHOW` and `HIDE`: update visibility; `SHOW` also raises the window to the
  front of the stable z-order;
- `MOVE`: changes signed screen position and damages both old and new bounds;
- `RESIZE`: transfers and atomically installs a new correctly sized surface;
- `DESTROY`: releases a caller-owned window and reveals the windows below it;
- `FOCUS`: requests keyboard focus for one visible caller-owned window;
- `REPLY`: returns the result, exact window identity, and accepted content
  generation.

Every mutation is bound to sender PID plus process generation, request id,
window id plus generation, and a monotonically increasing content generation
where pixels change. The service rejects malformed ABI fields, wrong owners,
stale generations, duplicate content generations, invalid handle counts,
surface metadata mismatches, reordered or incomplete chunks, oversized
submissions, and nonzero unused rectangle slots without mutating valid state.
Only one chunk transaction is active at a time; it has an exact sender and a
bounded deadline. A message from another sender cannot cancel it.

## Window State And Z-Order

The server owns a fixed table of 12 slots. Each slot has a stable numeric id
and an incrementing generation, so reuse never revives a stale authority
token. An active entry records owner identity, content generation, signed
position, dimensions, visibility, and its place in a fixed-capacity z-order.

New visible windows are appended at the front. Hiding preserves table
ownership, showing raises to the front, and destruction removes only the
target slot while preserving the relative order of the others. Owners may
hold multiple windows. Owner death destroys every matching entry and releases
every received surface in arbitrary slot or z-order.

## Damage And Composition

Client rectangles use surface coordinates. `windowd` validates positive
extent, clips to the surface, translates with overflow-safe signed arithmetic,
then clips again to the display. Overlapping or touching screen rectangles are
merged into a 64-entry accumulator. A 65th independent rectangle collapses
the accumulator to one full-screen rectangle.

For every accepted screen-damage rectangle, the compositor:

1. fills that region with the opaque background;
2. visits visible windows from back to front;
3. intersects each window with both the display and damaged region;
4. copies only the intersecting opaque RGB/BGR pixels;
5. submits only the accepted screen damage to `displayd`.

The composite surface is display-sized and page-backed. Client surfaces arrive
with attenuated `READ | MAP` rights and are mapped read-only. Pixel copying
runs in user space; applications cannot write the composite or framebuffer.

Surface replacement and resize install a candidate only for validation and
presentation. If presentation fails, `windowd` restores the previous surface,
geometry, and composite state and releases the candidate. Damage remains
pending until the exact frame generation is acknowledged. Display reconnect
forces a complete recomposition and full-frame resubmission.

## Input Routing And Focus

`inputd` blocks on the kernel input queue and forwards only normalized keyboard
events in `OsWindowInputForward`. Each forward carries ABI version, a monotonic
input sequence, the original `OsInputEvent`, and its hardware timestamp.
`windowd` accepts this internal event only from the exact PID plus generation
currently registered as `input`; an input-service generation change resets the
transport sequence boundary.

`windowd` owns one focused window identity or no focus. An explicit successful
`FOCUS` selects a visible caller-owned window. Hiding, destroying, or losing
the focused owner transfers focus to the topmost remaining visible window, or
clears it when no candidate remains. A repeated focus request for the current
window emits no duplicate transition.

Applications receive `OsWindowEvent` IPC v2 events from the exact registered
window-service identity. Focus changes emit `FOCUS_OUT` before `FOCUS_IN` and
keyboard input emits `KEY`; every event carries the target window id plus
generation and one shared monotonic event sequence. The server verifies that
the target process identity is still alive immediately before nonblocking
delivery. Queue exhaustion drops bounded event traffic rather than blocking
the compositor or transferring input to another client.

## Current Limits

- at most 12 opaque windows and one current surface per window;
- RGB/BGR 32-bit surfaces no larger than the display;
- no alpha, decorations, shadows, animation, or separate compositor process;
- `SHOW` is the current explicit raise operation;
- PS/2 keyboard routing is complete; pointer devices, hit testing, capture, and
  global shortcuts remain follow-up work;
- the public Window SDK now owns ordinary client transport, mapped drawing,
  damage chunking, information queries, and event validation; explicit
  integration producers remain only for lower-level protocol regression.

## Verification

```sh
make test-window-contracts
make test-window-single
make test-window-multi-contracts
make test-window-multi
make test-window-input-contracts
make test-window-input
make test-window-sdk-contracts
make test-gui-app
```

The host targets cover ABI layout, ownership and generation denial, 12-slot
capacity, slot reuse, z-order, hide/show, movement, resize state, four-edge
clipping, deterministic frame hash, partial composition, malformed chunks,
overflow-safe rectangle arithmetic, and full-screen accumulator collapse.

The QEMU targets retain the full-screen Phase 4D lifecycle and add two
restricted concurrent clients. Pixel evidence verifies overlap, hide/reveal,
raise, clipped movement, atomic resize, and chunked partial damage. Both
destruction orders are accepted and the final active process, mapping, handle,
mailbox, service, shared-object, surface, and heap samples must match the
warmed supervised-service baseline.

The input targets add exact raw-input authority checks, transport and
application sequence validation, stale PID-generation rejection, focus
cleanup/reconnection source checks, and a two-client QEMU route. The QEMU test
proves keyboard delivery to only the focused visible owner, ordered focus
transfer on hide, and stable active resources after both client lifecycles.

The Window SDK targets add client ABI layout, correlated reply filtering,
unexpected-message preservation, canvas clipping, and a restricted QEMU GUI
application that replaces and damages its surface, resizes, processes focused
keyboard input, and exits without raw input or display authority.
