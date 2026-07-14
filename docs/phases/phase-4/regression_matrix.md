# Phase 4 Regression Matrix

This matrix maps each Phase 4 contract to automated evidence. Rows begin as
`Planned`; they become `Complete` only when the named target exists, its
positive and negative paths pass, and the corresponding evidence is recorded
in `progress.md`.

The final aggregate target will be:

```sh
make test-phase4
```

`make test-phase4` does not exist yet and must not be reported as passing until
Phase 4H wires the completed focused targets into it. Phase 4 closure also
requires the existing full `make test-closure` suite.

## Regression Matrix

| ID | Subphase | Contract | Automated evidence | Pass condition | Status |
| --- | --- | --- | --- | --- | --- |
| P4-R01 | 4A | Surface storage is PMM-page-backed, zero-filled, bounded, and rollback safe | `make test-surface-backing`; `make test-graphics`; `make test-fault-injection`; `make test-closure` | Valid bounds allocate correctly; invalid/partial allocations release every page and object; existing graphics behavior remains intact. | Complete |
| P4-R02 | 4B | User surface mapping, rights attenuation, transfer lifetime, and process cleanup are enforced | `make test-surface-abi`; `make test-user-sdk`; `make test-service-manager-smoke`; `make test-concurrency`; `make test-closure` | App write mapping and compositor read mapping work; escalation fails; exit order leaves zero unexplained handle, mapping, or page drift. | Complete |
| P4-R03 | 4C | Only `displayd` can present, and its chunked generation protocol is bounded | display protocol host tests; display-service QEMU smoke; permission denial tests | Valid frames are acknowledged; malformed/stale transactions are rejected; ordinary apps cannot present; terminal fallback survives restart. | Planned |
| P4-R04 | 4D | One full-screen client reaches the display only through `windowd -> displayd` | single-window protocol tests; deterministic QEMU first-window smoke | Create/attach/damage/destroy succeeds without direct display authority and unexpected client exit cleans all state. | Planned |
| P4-R05 | 4E | Multiwindow z-order, clipping, damage merge, and composition stay within fixed bounds | compositor host pixel/hash tests; multiwindow QEMU smoke | Up to 12 windows compose deterministically; malformed damage is safe; accumulator overflow becomes one full-screen rectangle. | Planned |
| P4-R06 | 4F | Input reaches exactly one valid focused window with ordered focus events | input-routing host tests; keyboard-focus QEMU smoke | Background, hidden, destroyed, and stale-generation windows receive no key events; focus order and sequences remain valid. | Planned |
| P4-R07 | 4G | A normal application can use only the public SDK for its complete window lifecycle | ABI/layout tests; SDK integration suite; first-GUI-app QEMU smoke | The demo creates, draws, receives input, damages, resizes, and exits without raw `INPUT` or `DISPLAY`. | Planned |
| P4-R08 | 4H | Client and GUI-service failure paths recover or enter bounded fallback without leaks | fault-injection matrix; service restart QEMU smoke | Failure at every allocation/protocol boundary rolls back; restart limits hold; terminal fallback remains usable. | Planned |
| P4-R09 | 4H | Repeated GUI lifecycle and service churn has no unexplained resource drift | 60-second Phase 4 soak | Warmed and final handle/page/region/object/process/service samples match after allowed caches stabilize. | Planned |
| P4-R10 | 4H | Phase 4 and all earlier contracts pass together from a clean tree | `make test-phase4`; `make test-closure`; clean parallel UEFI build and QEMU smoke | Every focused row and the full closure suite pass; docs contain exact commands, results, commits, and measured evidence. | Planned |

## Evidence Rules

- Replace a planned description with the exact Make target once that target is
  added; never cite a command that does not exist.
- Record both positive and denial/malformed paths for every privilege or ABI
  boundary.
- Host-only tests cannot close a row that promises QEMU behavior.
- A screenshot is optional evidence, not a substitute for serial markers,
  pixel/hash checks, resource counts, and exit status.
- Rerunning a test after later changes updates the evidence date and result but
  does not rewrite the original implementation commit.
- Any failure in P4-R01 through P4-R09 blocks P4-R10 and Phase 4 closure.

## Closure Results

P4-R01 was certified on 2026-07-14 at implementation commit `196339c`.
Its focused host/QEMU target, existing graphics suite, fault-injection suite,
clean build smoke, and full closure all passed with zero surface-page drift.
P4-R02 was certified on 2026-07-15 at implementation commit `b14bca1`.
The public surface ABI, per-process NX mappings, read-only non-transferable IPC
clones, sender/receiver exit ordering, failure rollback, restricted permission
profile, and QEMU resource accounting passed with zero drift. P4-R03 through
P4-R10 remain uncertified. Phase 4H will add the final tested
commit, aggregate command results, durations, relevant counts, QEMU display
evidence, and 60-second soak measurements. The optional one-hour release soak
remains separate from the ordinary Phase 4 closure run.
