# OS64 Documentation

This directory is the entry point for OS64 design, reference, testing, and
phase records. Repository-relative paths are used throughout the documents so
links and test references remain stable from the project root.

## Project Direction

- [Roadmap](roadmap.md)
- [Project history](history/README.md)

## Architecture

- [2D graphics library](architecture/2d_graphics_library.md)
- [ACPI power-off](architecture/acpi_power.md)
- [Concurrency readiness](architecture/concurrency_readiness.md)
- [IPC v2](architecture/ipc_v2.md)
- [Kernel context and concurrency rules](architecture/kernel_context_rules.md)
- [Kernel objects and handles](architecture/kernel_handles.md)
- [Process and scheduler invariants](architecture/process_scheduler_invariants.md)
- [Service supervision and permissions](architecture/service_supervision.md)

## Reference

- [Driver ABI](reference/driver_abi.md)
- [Driver settings and product policy](reference/driver_policy.md)
- [User SDK v2](reference/user_sdk.md)

## Testing

- [Fault injection and soak testing](testing/fault_injection_and_soak.md)

## Phase Records

- Phase 1: [regression matrix](phases/phase-1/regression_matrix.md)
- Phase 2: [task breakdown](phases/phase-2/task_breakdown.md),
  [regression matrix](phases/phase-2/regression_matrix.md)
- Phase 3: [task breakdown](phases/phase-3/task_breakdown.md),
  [regression matrix](phases/phase-3/regression_matrix.md)
- Phase 3.5: [starting baseline](phases/phase-3.5/baseline.md),
  [stabilization](phases/phase-3.5/stabilization.md),
  [ABI freeze](phases/phase-3.5/abi_freeze.md),
  [regression matrix](phases/phase-3.5/regression_matrix.md)
- Phase 3.6: [driver packaging and layout](phases/phase-3.6/driver_packaging.md),
  [regression matrix](phases/phase-3.6/regression_matrix.md)
- Phase 4: [entry baseline](phases/phase-4/entry_baseline.md),
  [compositor and window service contracts](phases/phase-4/compositor_contracts.md),
  [implementation plan](phases/phase-4/implementation_plan.md)
