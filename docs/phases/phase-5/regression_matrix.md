# Phase 5 Regression Matrix

This matrix defines the evidence required to close the native desktop
foundation. Every command below is a reserved target name until its subphase
implements it. A reserved name is not evidence and must not be added as an
empty target.

## Matrix

| ID | Subphase | Contract | Planned automated evidence | Pass condition | Status |
| --- | --- | --- | --- | --- | --- |
| P5-R01 | 5A | One generation-owned graphical session controls startup, shutdown, and recovery. | `make test-desktop-session` | Ordered start/stop, duplicate requests, stale generations, service crashes, restart exhaustion, and console fallback are deterministic. | Complete |
| P5-R02 | 5A | Desktop, normal, panel, and overlay layers enforce exact authority and z-order. | `make test-desktop-layers` | Ordinary clients cannot select privileged layers; clipping, overlap, teardown, and session rollover preserve order and resources. | Complete |
| P5-R03 | 5B | Pointer input has one owner, bounded coordinates, temporal focus, and capture. | `make test-pointer-routing` | Motion/button/wheel, screen edges, click focus, overlapping hit tests, capture loss, stale targets, and queue overflow are correct. | Complete |
| P5-R04 | 5B | Interactive decorations perform atomic, server-authoritative window operations. | `make test-window-interaction` | Drag, edge/corner resize, close, minimize, maximize, restore, cancellation, and client failure never expose partial geometry. | Complete |
| P5-R05 | 5C | Alpha, font, native/BMP/PNG image, clipping, scaling, layout, and damage primitives are bounded and deterministic. | `make test-image-codecs`; `make test-ui-rendering` | Positive and malformed image fixtures, decode/work budgets, golden pixels, distinct upper/lowercase and punctuation glyphs, malformed UTF-8, clipping edges, aspect-preserving scaling, responsive resize, and full-repaint fallback pass. | Complete |
| P5-R06 | 5C | Public widgets preserve input, focus, state, resize, and cleanup contracts. | `make test-widget-sdk`; `make test-responsive-window` | A restricted client operates every baseline widget, shows a text caret, replaces surfaces and recomputes layout across configure events, rejects stale/invalid resize, and exits without raw display/input access or resource drift. | Complete |
| P5-R07 | 5D | A GUI terminal owns a bounded terminal stream and one child-shell lifecycle. | `make test-terminal-model`; `make test-gui-terminal` | Command input/output, ANSI baseline, scrollback, resize, backpressure, hangup, child fault, and repeated teardown pass. | Complete |
| P5-R08 | 5E | The desktop shell discovers, launches, activates, minimizes, and restores applications. | `make test-desktop-shell` | Background/panel persistence, launcher/task state, shortcuts, desktop restart, and surviving applications remain coherent. | Planned |
| P5-R09 | Memory gate | PMM capacity derives from valid firmware memory metadata rather than a 512 MiB compile-time ceiling. | `make test-pmm-scaling` | Multiple RAM sizes, memory above 4 GiB, holes, overflow, invalid maps, SMP allocation, and reserved ranges pass fail-closed checks. | Planned |
| P5-R10 | 5F | Directory and file mutation APIs are bounded, race-aware, and recover cleanly. | `make test-vfs-desktop` | Enumeration/stat/create/rename/remove/copy/move, disappearing entries, read-only targets, and injected failures preserve data and handles. | Planned |
| P5-R11 | 5F | The file manager remains responsive across valid, empty, large, and failing directories. | `make test-file-manager` | Navigation, selection, operations, associations, dialogs, crash/restart, and repeated open/close produce exact resource cleanup. | Planned |
| P5-R12 | 5G | Settings and power UI enforce versioning, persistence, permissions, and recovery. | `make test-settings-ui` | Valid, malformed, old/future, denied, interrupted-write, shutdown, reboot, and restart cases are deterministic. | Planned |
| P5-R13 | 5H | Installed application discovery follows one documented layout and rejects malformed content. | `make test-installed-layout` | Clean discovery, duplicate/version conflict, missing fields, path escape, permission mismatch, and developer/installed image assembly pass. | Planned |
| P5-R14 | 5I | Desktop failures and sustained mixed workloads preserve recovery and exact resources. | `make test-desktop-faults`; `make test-desktop-soak`; `make test-phase5` | One/four-vCPU faults and a required 60-second workload pass with console recovery, bounded queues, zero lock violations, and identical warmed/final active resources. | Planned |
| P5-R15 | 5I | Every inherited kernel, driver, service, thread, SMP, and Phase 4 GUI contract remains valid. | `make test-current-closure` | The complete inherited aggregate passes after all Phase 5 changes. | Planned |

## Required Resource Evidence

Every resource-sensitive target records warmed and final values for:

- session, service, process, thread, and child lifecycle records;
- surfaces, mappings, window slots, layer slots, damage, and cursor/capture
  state;
- handles, IPC/terminal queues, dropped events, and blocked waiters;
- widget, terminal scrollback, directory enumeration, and settings buffers;
- PMM free pages, heap use, address spaces, and TLB quarantine;
- driver, MMIO, DMA, IRQ, and quiesce state inherited from Phase 4.7.

Expected cache/high-water growth is recorded separately. Any unexplained live
resource, lock, queue, or ownership drift blocks completion.

## Mandatory Negative Coverage

- stale session/window/process/thread/handle generations;
- unauthorized layer, focus, capture, settings, power, and file operations;
- pointer coordinate, geometry, surface/image stride, alpha, text, layout, and
  size overflow;
- truncated/oversized image headers, unsupported BMP/PNG variants, invalid PNG
  chunk/CRC/filter/DEFLATE streams, decompression bombs, partial publication,
  and image-buffer ownership errors;
- queue full, partial IPC transaction, peer exit, restart exhaustion, and
  dependency failure;
- invalid UTF-8, terminal control sequence, path, directory entry, settings
  record, and application manifest;
- surface/process/PMM exhaustion and failure at each allocation or publication
  boundary;
- service or application crash during pointer capture, resize, repaint, PTY
  traffic, file mutation, settings write, launch, and session shutdown;
- one-vCPU fallback and four-vCPU concurrency.

## Evidence Rules

- host tests may close parsers and isolated state machines, but QEMU execution
  is mandatory for decoded image presentation, pointer delivery, display
  pixels, session recovery, PTY scheduling, file operations, SMP races, and
  shutdown.
- each focused row must pass independently before the aggregate can count.
- screenshots may supplement evidence but cannot replace exact state, pixel,
  lifecycle, and resource assertions.
- P5-R01 through P5-R15 all block Phase 5 closure.
- the optional one-hour soak does not replace the required repeatable
  60-second soak.
