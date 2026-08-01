# Phase 5 Entry Baseline

This document records the system from which Phase 5 begins. It is a baseline,
not a claim that desktop functionality already exists.

## Entry Gate

Phase 4 through Phase 4.7 are closed. The post-closure corrective audit is
recorded in commit `b73f854`.

- UEFI build: PASS;
- Phase 4 window, display, input, lifecycle, and recovery gates: PASS;
- Phase 4.5 thread model and one-CPU concurrency gates: PASS;
- Phase 4.6 SMP, IPI, TLB shootdown, lock, fault, and soak gates: PASS;
- Phase 4.7 driver ownership, MMIO, DMA, IRQ, quiesce, and soak gates: PASS;
- required 60-second driver, thread, GUI, and service workloads have passed;
- the optional one-hour release soak is not an entry requirement.

## Existing Graphical Foundation

The system already provides:

- page-backed user surfaces with generation-checked mapping and transfer
  rights;
- a display-service present path with a GOP fallback backend;
- supervised `windowd` and bounded multiwindow z-order and composition;
- opaque 32-bit surfaces, clipping, damage, movement, resize, show/hide,
  replacement, focus, and deterministic destruction;
- focused keyboard input routed through `inputd`;
- a public Window SDK and an event-driven GUI sample application;
- deterministic console/GUI ownership handoff and console recovery;
- drive-free preemptive scheduling, multiple threads, and SMP execution.

## Missing Desktop Foundation

The current stack intentionally does not yet provide:

- pointer hardware, cursor composition, hit testing, or capture;
- interactive decorations, title bars, drag, or pointer resize;
- alpha composition, general fonts, image loading/decoding, widgets, layout,
  or themes;
- a session supervisor or persistent desktop shell;
- a GUI terminal or PTY-style terminal transport;
- a file manager or the required directory/stat/mutation VFS SDK;
- a settings store and graphical settings/power UI;
- installed-application manifests and a stable installed-system layout.

## Existing Layer Boundary

Phase 4 uses transitional layers:

```text
CONSOLE_UNDERLAY
NORMAL_WINDOW
SYSTEM_OVERLAY
```

Phase 5 replaces normal desktop presentation with:

```text
DESKTOP
NORMAL_WINDOW
PANEL
SYSTEM_OVERLAY
```

Layer policy belongs to `windowd`. The kernel enforces display-session
ownership, handles, process permissions, and the recovery boundary.

## Memory And Capacity Baseline

- the physical memory manager currently has a fixed 512 MiB ceiling;
- process capacity is 16;
- thread capacity is eight per process;
- the compositor has a bounded 12-window model;
- surfaces and GUI/service churn have exact warmed/final resource accounting.

These bounds are sufficient for early session, pointer, toolkit, and terminal
bring-up. Before Phase 5F file-manager and multi-application closure work, the
PMM must derive its metadata and managed range from the boot memory map rather
than a fixed 512 MiB constant. Capacity increases must preserve bounded failure
and exact cleanup instead of merely raising constants without tests.

## Entry Decision

The Phase 5 entry evidence is present. Phase 5A may begin. Phase 5 completion
remains blocked on every planned regression row and the inherited closure
suite.
