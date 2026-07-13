# Phase 4 Entry Baseline

- Date: 2026-07-13
- Tag: `phase-4-entry`
- Commit: `53053a6` (`Complete Phase 4 entry preflight`)
- Repository snapshot: 155 commits, 365 tracked files, 50,092 total lines

This milestone freezes the stable starting point for the compositor and window
system phase. BootInfo v3 and the signed boot-driver handoff were exercised in
QEMU, including provider-before-consumer ordering and rejection of unsigned,
tampered, oversized, and dependency-invalid modules.

The compositor, window service, display ownership, input routing, and page-
backed user-surface contracts were documented before implementation. The Phase
4 entry gate passed the ABI, SDK, graphics, input, IPC, service, concurrency,
fault-injection, driver-policy, UEFI, and 60-second soak suites. The optional
one-hour rerun was excluded from this preflight by request; the earlier Phase
3.5 release gate remains recorded separately.

## Records

- [Entry baseline](../../phases/phase-4/entry_baseline.md)
- [Compositor and window service contracts](../../phases/phase-4/compositor_contracts.md)
- [Roadmap](../../roadmap.md)
