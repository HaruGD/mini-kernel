# First Supervised Window

- Date: 2026-07-15
- Tag: `first-window`
- Commit: `231f27e` (`feat(window): complete phase 4D single-window path`)
- Repository snapshot: 168 commits, 416 tracked files, 57,217 total lines

This milestone records the first client-owned GUI surface reaching the screen
through the intended user-space chain: a restricted application transfers its
surface to supervised `windowd`, `windowd` composites into its own retained
surface, and supervised `displayd` alone presents that result through the GOP
backend. The client has no direct display permission.

The Phase 4D implementation provides one full-screen opaque window, exact
process and content generations, transactional surface replacement, normal
destroy, and cleanup when the owner exits without destroying its window. Host
contract tests and QEMU pixel probes verify the result; full Phase 4
multiwindow composition, focus routing, and the public high-level window SDK
remain future milestones.

## Records

- [Phase 4 progress](../../phases/phase-4/progress.md#4d-single-window-bring-up)
- [Phase 4 regression matrix](../../phases/phase-4/regression_matrix.md)
- [Window service reference](../../reference/window_service.md)
- [Roadmap](../../roadmap.md)
