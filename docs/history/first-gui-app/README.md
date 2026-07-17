# First Public-SDK GUI Application

- Date: 2026-07-17
- Tag: `first-gui-app`
- Commit: `4bafe1f` (`feat(window): complete phase 4G SDK and GUI app`)
- Repository snapshot: 175 commits, 429 tracked files, 62,921 total lines

This milestone records the first ordinary event-driven GUI application built
entirely on the public OS64 Window SDK. The restricted application creates and
maps a surface, draws through software canvas helpers, submits bounded damage,
receives focus-routed keyboard events, replaces and resizes its surface, and
destroys the window cleanly.

The application has neither raw input nor direct display authority. Input
travels through `inputd` and `windowd`; window content reaches the screen
through `windowd` and the sole normal present authority, `displayd`. Host
contract tests validate ABI layout, correlated IPC, unexpected messages,
malformed replies, clipping, and permissions, while the QEMU smoke validates
initial pixels, F1 redraw, F2 resize, Escape shutdown, and stable active
resources.

Phase 4H fault injection, GUI churn, service restart recovery, soak testing,
and aggregate closure remain after this milestone. Widgets, desktop shell,
decorations, alpha effects, and a separate compositor process remain later
work.

## Records

- [Phase 4G progress](../../phases/phase-4/progress.md#4g-window-sdk-and-first-gui-application)
- [Phase 4 regression matrix](../../phases/phase-4/regression_matrix.md)
- [Window SDK reference](../../reference/window_sdk.md)
- [Window service reference](../../reference/window_service.md)
- [Roadmap](../../roadmap.md)
