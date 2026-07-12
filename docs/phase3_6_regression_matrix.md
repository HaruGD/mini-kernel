# Phase 3.6 Driver Packaging Regression Matrix

This matrix closes the driver policy, project-layout, build-integration, and
staged-activation migration before Phase 4. Every driver remains a package
project; `config/drivers.json` decides whether its current product artifact is
kernel-linked or a signed `.drv` and when it activates.

Run the complete matrix with:

```sh
make test-driver-regression
```

The target combines host policy/generation checks, a real UEFI driver smoke,
and User SDK filesystem coverage. `make test-closure` also includes it.

## Current Product Activation Plan

| Stage | Linked automatic | Packaged automatic | Packaged manual |
| --- | --- | --- | --- |
| Boot | `gop`, `terminal` | none | forbidden |
| Kernel | `ata0`, `fat32`, `keyboard`, `pit` | none | forbidden |
| Runtime | none | `provider_c`, `consumer_c`, `hello_c`, `hello_cpp`, `irq_timer_c`, `pci_probe_c` | `gop_demo_c` |

Changing a policy entry regenerates these sets without moving the project.
Dependencies are emitted before dependents; `provider_c` therefore precedes
`consumer_c` in the runtime plan.

## Regression Matrix

| ID | Contract | Automated evidence | Pass condition |
| --- | --- | --- | --- |
| R01 | Central policy and local settings validate | `make test-driver-policy` | All projects are listed once; invalid fields, paths, capabilities, dependencies, and forbidden combinations fail. |
| R02 | Unified project shape and local Make interface | `make test-driver-layout` | Every policy path has `settings.json` and `Makefile`; legacy mechanism directories and `driver.json` files are absent. |
| R03 | Policy drives artifact build and root-image shipping | `make test-driver-build`, `make uefi` | Enabled linked objects enter `kernel64.bin`; enabled `.drv` packages are signed and shipped; disabled entries produce nothing. |
| R04 | All seven allowed combinations partition correctly | `tools/driver_build_integration_test.py` | Three linked-automatic, three packaged-automatic, and one packaged-runtime-manual fixture enter exactly their generated set. |
| R05 | Forbidden policy combinations and dependency violations fail | `tools/driver_policy_test.py` | Linked-manual, boot/kernel-manual, later-stage, automatic-to-manual, missing, and cyclic dependencies are rejected. |
| R06 | Boot `.drv` handoff is bounded and verified | `make test-driver-boot` | Real QEMU boots verify signed activation and dependency order; unsigned, tampered, oversized, excessive-count, and dependency-invalid modules are rejected without corrupting boot. |
| R07 | Linked and packaged activation uses exact stages | generated-source assertions in `tools/driver_build_integration_test.py` | Boot, kernel, and runtime functions contain only their policy-selected drivers and retain dependency order. |
| R08 | Driver list remains behaviorally equivalent | `make test-uefi-smoke` | `drivers` reports linked hardware/filesystem records and the expected ready packaged records. |
| R09 | Automatic and manual loading remain distinct | `make test-uefi-smoke` | Six runtime-automatic demo packages activate once; `gop_demo_c` remains inactive until explicit `drvload`. |
| R10 | Manual lifecycle commands remain valid | `make test-uefi-smoke` | `drvload`, `drvunload`, and `drvreload` succeed where allowed, and dependent unload protection remains enforced. |
| R11 | FAT32 root and file I/O survive the move | `make test-uefi-smoke`, `make test-user-sdk` | The ramdisk mounts as FAT32 `/`; directory listing and SDK create/read/write/seek/remove coverage pass. |
| R12 | Documentation and matrix stay synchronized | `tools/driver_regression_matrix_test.py` | README/ABI responsibilities, links, policy sets, Make target wiring, and all Phase 4 entry checks remain present. |

## Failure Interpretation

- R01/R02 failures indicate policy or package ownership drift.
- R03/R04/R07 failures indicate generated build or activation drift.
- R06 failures block boot-module use even if ordinary runtime packages work.
- R08–R11 failures are runtime regressions and block Phase 4 entry.
- R12 failures mean implementation and operator documentation disagree.

Phase 4 work must not begin while any row is failing.
