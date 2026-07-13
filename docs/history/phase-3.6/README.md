# Phase 3.6 Driver Packaging Complete

- Date: 2026-07-13
- Tag: `phase-3.6-complete`
- Commit: `5107211` (`Close Phase 3.6 driver packaging`)
- Repository snapshot: 154 commits, 361 tracked files, 49,563 total lines

Phase 3.6 made driver packaging policy explicit and reproducible. Every driver
project received a local `Makefile` and `settings.json`, while
`config/drivers.json` became the central product policy for linked and `.drv`
builds, load stages, automatic activation, and build inclusion.

The unified driver project shape, policy validation, build integration, and the
seven permitted policy combinations were covered by regression tests. Linked
boot and kernel drivers coexist with signed runtime `.drv` modules under one
documented policy model.

## Records

- [Driver packaging and layout](../../phases/phase-3.6/driver_packaging.md)
- [Regression matrix](../../phases/phase-3.6/regression_matrix.md)
- [Driver ABI](../../reference/driver_abi.md)
- [Driver settings and product policy](../../reference/driver_policy.md)
