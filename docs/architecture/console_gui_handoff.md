# Console And GUI Display Handoff

This document defines the Phase 4H transition between the boot/kernel console
and the graphical window stack. It is a prerequisite for a desktop shell, but
it does not make the kernel console itself part of the desktop.

Implementation status (2026-07-20): the 4H-A session state machine, retained
read-only console underlay, exclusive scanout/input routing, last-window
release, and process-exit recovery are implemented. Failure injection,
restart-limit fallback, resource pressure, repeated churn, and soak evidence
remain Phase 4H-C work, so this is not yet a Phase 4 completion claim.

## Problem

Before 4H-A, the display-owner token serialized only individual framebuffer
operations. It did not grant a persistent display mode to the graphical
session, so kernel terminal output could acquire the GOP owner between GUI
presents and overwrite pixels already composed by `windowd`.

This was visible in the earlier QEMU pixel tests: serial/status output could obscure a
portion of an otherwise valid window frame. Raising a window in z-order cannot
solve the problem because the terminal renderer is outside the compositor.

Phase 4 must not close with two independent renderers writing the scanout
surface. Once GUI mode is active, every ordinary visible pixel must come
through `windowd -> displayd -> display backend` until ownership returns to the
console.

## Target Model

The first transitional GUI mode preserves the console image as an immutable
underlay and composites normal windows above it:

```text
GUI inactive
  kernel console RAM buffer -> GOP scanout

first visible GUI window
  capture retained console snapshot
  freeze direct terminal scanout writes
  acquire GUI display session

GUI active
  CONSOLE_UNDERLAY
  NORMAL_WINDOW
  SYSTEM_OVERLAY
        -> windowd composite
        -> displayd present
        -> GOP scanout

last GUI window closes or GUI stack fails
  release GUI display session
  redraw retained live console buffer
  restore console input and scanout ownership
```

The underlay is a compatibility bridge for bring-up. The final desktop uses a
desktop-shell surface instead of treating the kernel console as wallpaper.

## Display Session State

The kernel display boundary needs an explicit generation-tagged session state:

| State | Visible owner | Allowed ordinary scanout writes |
| --- | --- | --- |
| `CONSOLE_ACTIVE` | kernel terminal | terminal renderer and panic path |
| `GUI_ACQUIRING` | handoff transaction | no new ordinary terminal scanout writes |
| `GUI_ACTIVE` | exact registered `displayd` PID plus generation | accepted display-service presents only |
| `CONSOLE_RESTORING` | handoff transaction | console full redraw only |
| `FALLBACK` | kernel terminal | bounded recovery and diagnostic output |

Every acquisition receives a nonzero display-session generation. Present,
release, and recovery operations must match the current owner identity and
session generation. A restarted service cannot reuse a stale lease.

The panic renderer may override ordinary ownership because reporting a fatal
kernel failure is more important than preserving the GUI frame. That emergency
path must be explicit and must not become a general terminal-output bypass.

## Acquisition

For the Phase 4 bring-up path, `windowd` requests GUI activation when it is
ready to show the first visible normal window. The acquisition is successful
only when:

- the exact registered `displayd` identity is alive and owns normal `DISPLAY`
  authority;
- `windowd` and `displayd` agree on display dimensions, stride, and format;
- a full retained console snapshot has been created without exposing the
  physical framebuffer address to user space;
- the kernel terminal has stopped issuing ordinary scanout writes;
- input ownership can transfer atomically with the visible mode.

The preferred implementation copies the terminal's RAM-backed render state
into a page-backed snapshot surface and transfers only `READ | MAP` authority
through the trusted display/window handoff. User space must never map GOP MMIO
or receive a writable handle to kernel console storage. If the snapshot cannot
be created, acquisition fails and the console stays active.

After acquisition, `windowd` initializes its composite from the snapshot and
retains it as `CONSOLE_UNDERLAY`. The underlay:

- always covers the display;
- is below every normal window;
- never receives focus, keyboard, pointer, resize, move, or client damage;
- is not counted against ordinary application window capacity;
- is replaced only by a new display-session acquisition or final desktop
  background policy.

## Logging While GUI Mode Is Active

Kernel and service diagnostics must remain observable without modifying the
GUI frame:

- serial output continues normally;
- the kernel log ring continues recording normally;
- terminal text state continues updating in RAM so it can be redrawn later;
- ordinary terminal glyph and scroll operations do not touch GOP scanout;
- service logs do not trigger ownership changes or partial console presents.

This separates logging from rendering. A later GUI log viewer or terminal can
read an authorized stream or file; Phase 4H only requires lossless retained
state and console restoration.

## Input Ownership

Input must follow the same visible-mode transition:

- in `CONSOLE_ACTIVE`, keyboard input targets the kernel console;
- in `GUI_ACTIVE`, `inputd` consumes normalized input and `windowd` routes it
  only to the focused GUI window;
- the console underlay never receives GUI focus;
- the same physical event cannot be delivered to both console and GUI paths;
- events timestamped before acquisition cannot become GUI application input;
- after restoration completes, new input targets the console again.

The initial policy releases GUI mode when the last visible normal window is
destroyed and no replacement is pending. A later desktop session remains in
GUI mode because the desktop-shell window is persistent.

## Restoration And Failure

Normal restoration proceeds as one transaction:

1. stop accepting new GUI presents for the old session generation;
2. clear GUI focus and cancel or drain pending application input safely;
3. detach and release the console underlay snapshot;
4. switch to `CONSOLE_RESTORING`;
5. redraw the current retained terminal state as one full console frame;
6. restore console input ownership;
7. enter `CONSOLE_ACTIVE` with a new ownership generation.

The same bounded restoration is required when `windowd` or `displayd` crashes,
loses registration, exceeds its restart limit, or submits an unrecoverable
present failure. Cleanup must be idempotent because process-exit cleanup,
service supervision, and the display boundary may observe the same failure.

If a normal full redraw fails, enter `FALLBACK`, keep serial/klog diagnostics
available, and retry only through a bounded recovery path. Never leave a dead
GUI owner token blocking panic or console recovery.

## Window Layers

The Phase 4H transition requires only three policy layers:

```text
CONSOLE_UNDERLAY  fixed session-owned bottom content
NORMAL_WINDOW     ordinary application windows
SYSTEM_OVERLAY    bounded trusted diagnostics or transition UI
```

Ordinary applications cannot select `CONSOLE_UNDERLAY` or `SYSTEM_OVERLAY`.
The first implementation may keep these layers outside the ordinary 12-slot
window table as fixed compositor inputs.

Phase 5 replaces the transitional layout with:

```text
DESKTOP           desktop-shell background
NORMAL_WINDOW     applications
PANEL             desktop-shell panel/dock
SYSTEM_OVERLAY    trusted session UI
```

Layer policy belongs to `windowd`, not the kernel. The kernel enforces only the
display-session owner, handle rights, permissions, and recovery boundary.

## Relationship To The GUI Terminal

The preserved kernel console is not the final graphical terminal. It is a boot,
diagnostic, and failure-recovery virtual console. A normal shell inside the
desktop will eventually run in a GUI terminal application using ordinary
Window SDK surfaces and input events.

A later virtual-terminal switch may move between text and graphical sessions
without destroying either session. Phase 4H requires only one active visible
mode and deterministic first-window/last-window handoff.

## Future Windows Presentation Domain

The post-desktop [Windows GUI domain architecture](windows_gui_domain.md)
extends this ownership model with another optional presentation backend. It
does not let Windows replace the native handoff or become the authority for
OS64 windows, physical input, permissions, or recovery.

The future state machine starts from a fully functional native display mode,
boots and negotiates the Windows domain, and returns to native output whenever
the VM, Guest Agent, DWM, assigned GPU, output switch, or bridge stops making
progress. Phase 4H therefore remains a prerequisite: native applications and
their Host-owned surfaces must survive a presentation-domain failure and be
redrawn without Guest cooperation.

## Required Regression Evidence

Phase 4H must add host contract tests and QEMU integration evidence for:

- exact state and session-generation transitions;
- stale PID-generation and stale display-session release/present rejection;
- preserved console pixels below the first GUI window;
- normal windows always appearing above the console underlay;
- kernel/service logging during GUI mode producing zero scanout pixel changes;
- focused GUI input with zero duplicate console delivery;
- last-window close restoring current console contents and command input;
- `windowd` crash, `displayd` crash, restart-limit exhaustion, and failed
  acquisition restoring or entering bounded console fallback;
- repeated acquire/release cycles with zero unexplained surface, mapping,
  handle, owner-token, process, heap, or PMM drift;
- panic output remaining available regardless of the active GUI owner.

The existing screenshot threshold that tolerates console overwrites is
transitional. Phase 4H must replace it with exact no-overwrite evidence rather
than weakening the threshold further.

## Phase Order

```text
4G public Window SDK and first GUI app
    -> 4H-A console/GUI display and input handoff
    -> 4H-B drive-free single-CPU scheduling
    -> 4H-C lifecycle, crash, resource-pressure, and soak closure
    -> 4.5 threading foundation
    -> 4.6 SMP
    -> 5 desktop shell and GUI applications
```
