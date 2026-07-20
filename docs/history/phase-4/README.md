# Phase 4 GUI Foundation Complete

- Date: 2026-07-20
- Tag: `phase-4-complete`
- Implementation commit: `8d449297255858038e95c74f4f9f0d74a57c77b7`
- Commits: 186
- Tracked files: 440
- Tracked lines: 66,615

## Milestone

Phase 4 established the first complete native GUI foundation:

- page-backed graphics surfaces with bounded user mappings and attenuated
  transfer rights;
- an exclusive `windowd -> displayd -> display backend` presentation path;
- deterministic single-window and bounded multiwindow composition;
- focused keyboard routing through `inputd` and the public Window SDK;
- the first event-driven GUI application with redraw, resize, focus, and
  teardown;
- a generation-tagged console/GUI session with an immutable retained-console
  underlay and deterministic recovery;
- drive-free single-CPU idle scheduling for services and GUI applications;
- automatic `displayd` and `windowd` crash recovery while a GUI client is
  active;
- fault-injection coverage, aggregate Phase 4 regression, and a 60-second GUI
  lifecycle/service-churn soak with zero unexplained active-resource drift.

## Verification

- clean `make -j2 uefi`: PASS;
- `make test-phase4`: PASS in 216.5 seconds;
- forced display/window service recovery: PASS, with console fallback and
  generation-safe GUI reconnection;
- GUI soak: PASS for 60 seconds, 35 application cycles, 140 window lifecycles,
  and seven service-stack restarts;
- GUI soak resources: baseline and final both
  `(4, 21, 1, 0, 4, 0, 1, 27173, 1944432, 1961984)`;
- `make test-closure`: PASS in 651.30 seconds;
- existing 60-second service soak inside closure: PASS with 42 cycles;
- optional one-hour release soak: not run and remains a separate release
  certification task.

## Next Boundary

Phase 4.5 separates schedulable threads from process-owned resources before
Phase 4.6 introduces multiple active CPUs. Desktop-shell, decorations, widgets,
alpha composition, and normal GUI applications build on that stable execution
and lifecycle foundation.
