# Phase 2 Task Breakdown

Phase 2 builds a reusable 2D graphics path and a common input-event path on
top of the existing GOP, syscall, process, and keyboard foundations.
The 2D graphics library design is documented in
`docs/2d_graphics_library.md`.

## Working Rules

- Complete one numbered task per commit unless two tasks are inseparable.
- Every task must keep `make test-phase1` passing.
- Add the smallest relevant test with the implementation; do not postpone all
  tests until the end of the phase.
- Keep hardware access in drivers, reusable drawing logic in the graphics
  library, and application policy in user space.
- Split files by responsibility when a module gains a second distinct job.
- Do not start compositor or window-manager policy during this phase.

## 2A. Graphics Contracts

- [x] **G01: Define graphics primitives**
  Add shared point, rectangle, surface, pixel-format, and color definitions.
  Completion: overflow-safe dimensions and ABI size assertions compile.

- [x] **G02: Add rectangle clipping helper**
  Implement intersection and framebuffer-bound clipping without drawing.
  Completion: empty, partial, full, and integer-overflow cases are tested.

- [x] **G03: Introduce a memory surface**
  Represent pixels, width, height, and stride independently from GOP MMIO.
  Completion: a RAM-backed surface can be created and bounds-checked.

- [x] **G04: Route pixel and fill operations through surfaces**
  Move reusable drawing loops out of the GOP hardware wrapper.
  Completion: current `put_pixel`, `fill_rect`, and clear behavior is retained.

## 2B. 2D Drawing

- [x] **G05: Add horizontal and vertical lines**
  Completion: clipped lines render at all four framebuffer edges.

- [x] **G06: Add general line drawing**
  Use an integer line algorithm with clipping.
  Completion: horizontal, vertical, diagonal, reversed, and off-screen cases pass.

- [x] **G07: Add opaque bitmap blit**
  Copy a source surface region into a destination surface.
  Completion: source and destination clipping are tested.

- [x] **G08: Add color-key bitmap blit**
  Completion: transparent-key pixels leave the destination unchanged.

- [x] **G09: Add built-in bitmap font data**
  Keep font data separate from drawing code.
  Completion: printable ASCII glyph lookup has a deterministic fallback.

- [x] **G10: Add glyph and text rendering**
  Completion: clipping, newline, fallback glyph, and bounded string rendering work.

- [x] **G11: Add a graphics demo program**
  Exercise lines, rectangles, bitmap blits, and text from user space.
  Completion: the demo exits cleanly and does not damage kernel memory.

## 2C. Double Buffering

- [x] **G12: Allocate the back buffer**
  Allocate from the kernel heap with checked framebuffer dimensions.
  Completion: allocation failure keeps direct GOP output operational.

- [x] **G13: Add full-frame present**
  Copy the back buffer to GOP while respecting both strides and pixel format.
  Completion: QEMU screen smoke confirms a nonblank frame.

- [x] **G14: Define display ownership**
  Serialize framebuffer terminal and graphics clients during a present.
  Completion: shell text and graphics writes cannot interleave mid-frame.

- [x] **G15: Add dirty-rectangle tracking**
  Start with a bounded rectangle list and merge overlapping regions.
  Completion: overflow falls back to one full-screen dirty rectangle.

- [x] **G16: Add partial present**
  Copy only tracked dirty rectangles and clear the tracker afterward.
  Completion: unchanged framebuffer regions remain byte-identical.

- [x] **G17: Extend screen smoke coverage**
  Capture full and partial presents at 1280x800 and one smaller resolution.
  Completion: pixel checks validate placement, clipping, and nonblank output.

## 2D. Input Event Queue

- [x] **I01: Define the common input event ABI**
  Add event type, timestamp, key code, modifiers, button, and pointer fields.
  Completion: kernel/user ABI size assertions match.

- [x] **I02: Add a bounded kernel event ring**
  Completion: FIFO order, wraparound, empty state, and full state are tested.

- [x] **I03: Define the overflow policy**
  Drop the oldest event and increment a visible counter.
  Completion: a forced overflow preserves the newest events and reports drops.

- [x] **I04: Convert PS/2 keyboard input into common events**
  Preserve the existing shell character path while adding event production.
  Completion: key up/down and modifier transitions are correct.

- [x] **I05: Add nonblocking event-read syscall**
  Completion: user space receives one event or a stable would-block result.

- [x] **I06: Add blocking event-read syscall**
  Sleep the caller until an event arrives without busy waiting.
  Completion: injected QEMU input wakes exactly one waiting process.

- [x] **I07: Reserve mouse event semantics**
  Finalize relative motion, absolute position, wheel, and button representation.
  Completion: synthetic mouse events round-trip through the ABI.

- [x] **I08: Add input diagnostics**
  Add queue depth, capacity, delivered count, and dropped count inspection.
  Completion: shell diagnostics do not consume queued events.

## 2E. Process Event Delivery

- [ ] **P01: Add a per-process event queue**
  Completion: queues are initialized and released with the process slot.

- [ ] **P02: Add focused-process selection**
  Keep focus policy minimal and expose an explicit kernel setter.
  Completion: invalid or exited targets are rejected.

- [ ] **P03: Route input to the focused process**
  Completion: background processes cannot consume focused input.

- [ ] **P04: Wake blocked focused processes**
  Completion: focus changes and process exit cannot leave stale waiters.

- [ ] **P05: Add a user event-loop sample**
  Completion: a user program blocks, receives keyboard events, and exits normally.

## 2F. Phase Closure

- [ ] **T01: Add `make test-graphics`**
  Cover clipping, drawing primitives, blit, text, and full/partial present.

- [ ] **T02: Add `make test-input`**
  Cover queue ordering, overflow, blocking wakeup, and focused delivery.

- [ ] **T03: Run the full regression suite**
  Run clean build, Phase 1, UEFI, userland, screen, SDK, graphics, and input tests.

- [ ] **T04: Update documentation and close Phase 2**
  Update README, roadmap, API references, and a Phase 2 regression matrix.

## Required Test Baseline

After every task:

```sh
make test-phase1
```

Before marking Phase 2 complete:

```sh
make clean && make all && make uefi
make test-phase1
python3 tools/uefi_smoke.py
python3 tools/uefi_userland_smoke.py
python3 tools/uefi_screen_smoke.py
make test-user-sdk
make test-graphics
make test-input
```
