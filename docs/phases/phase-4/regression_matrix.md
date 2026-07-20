# Phase 4 Regression Matrix

This matrix maps each Phase 4 contract to automated evidence. Rows begin as
`Planned`; they become `Complete` only when the named target exists, its
positive and negative paths pass, and the corresponding evidence is recorded
in `progress.md`.

The final aggregate target is:

```sh
make test-phase4
```

`make test-phase4` was added by Phase 4H and passed together with the existing
full `make test-closure` suite on 2026-07-20.

## Regression Matrix

| ID | Subphase | Contract | Automated evidence | Pass condition | Status |
| --- | --- | --- | --- | --- | --- |
| P4-R01 | 4A | Surface storage is PMM-page-backed, zero-filled, bounded, and rollback safe | `make test-surface-backing`; `make test-graphics`; `make test-fault-injection`; `make test-closure` | Valid bounds allocate correctly; invalid/partial allocations release every page and object; existing graphics behavior remains intact. | Complete |
| P4-R02 | 4B | User surface mapping, rights attenuation, transfer lifetime, and process cleanup are enforced | `make test-surface-abi`; `make test-user-sdk`; `make test-service-manager-smoke`; `make test-concurrency`; `make test-closure` | App write mapping and compositor read mapping work; escalation fails; exit order leaves zero unexplained handle, mapping, or page drift. | Complete |
| P4-R03 | 4C | Only `displayd` can present, and its chunked generation protocol is bounded | `make test-display-present`; `make test-kernel-handles`; `make test-ipc-contracts` | Valid frames are acknowledged; malformed/stale transactions are rejected; ordinary apps cannot present; terminal fallback survives restart. | Complete |
| P4-R04 | 4D | One full-screen client reaches the display only through `windowd -> displayd` | `make test-window-contracts`; `make test-window-single` | Host ownership/generation denials pass; create/attach/damage/destroy succeeds in QEMU without direct display authority; deterministic pixels appear and unexpected client exit cleans all transient state. | Complete |
| P4-R05 | 4E | Multiwindow z-order, clipping, damage merge, and composition stay within fixed bounds | `make test-window-multi-contracts`; `make test-window-multi` | Up to 12 windows compose deterministically; malformed/partial chunk transactions are safe; all four edges clip; hide/show/move/resize and arbitrary destruction work; accumulator overflow becomes one full-screen rectangle with zero active-resource drift. | Complete |
| P4-R06 | 4F | Input reaches exactly one valid focused window with ordered focus events | `make test-window-input-contracts`; `make test-window-input` | Background, hidden, destroyed, stale-generation, and pre-focus queued input never reaches the current client; focus order and sequences remain valid. | Complete |
| P4-R07 | 4G | A normal application can use only the public SDK for its complete window lifecycle | `make test-window-sdk-contracts`; `make test-gui-app` | The demo creates, draws, receives input, damages, replaces its surface, resizes, and exits while raw `INPUT` and `DISPLAY` calls are denied. | Complete |
| P4-R08 | 4H | Client and GUI-service failure paths recover or enter bounded fallback without leaks | `make test-fault-injection`; `make test-gui-recovery` | Failure at allocation/handle/IPC/service boundaries rolls back; active-session display/window crashes restore the console and reconnect through new generations; restart limits remain bounded. | Complete |
| P4-R09 | 4H | Repeated GUI lifecycle and service churn has no unexplained resource drift | `make test-gui-soak` | Warmed and final handle/page/region/object/process/service samples match after 60 seconds of GUI and service churn. | Complete |
| P4-R10 | 4H | Console and GUI have one persistent, generation-tagged visible display and input owner | `make test-display-handoff`; `make test-gui-recovery`; first/last-window QEMU smokes | Console pixels are retained below normal windows; GUI-time logging causes zero scanout changes; stale sessions fail; last-window close and GUI crashes restore current console contents and input without leaks. | Complete |
| P4-R11 | 4H | Ready services, foreground IPC clients, and GUI applications progress while the kernel shell is idle | `make test-drive-free-scheduler`; `make test-gui-app`; `make test-window-input`; service-manager QEMU smoke | Timer preemption, idle selection, and timer/IPC/input/child wakeups work without `drive` or `udrive_c.elf`; repeated service requests complete before shell/input restoration; the temporary command and helper are removed. | Complete |
| P4-R12 | 4H | Phase 4 and all earlier contracts pass together from a clean tree | `make test-phase4`; `make test-closure`; clean parallel UEFI build | Every focused row and the full closure suite pass; docs contain exact commands, results, commits, and measured evidence. | Complete |

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
- Any failure in P4-R01 through P4-R11 blocks P4-R12 and Phase 4 closure.

## Closure Results

P4-R01 was certified on 2026-07-14 at implementation commit `196339c`.
Its focused host/QEMU target, existing graphics suite, fault-injection suite,
clean build smoke, and full closure all passed with zero surface-page drift.
P4-R02 was certified on 2026-07-15 at implementation commit `b14bca1`.
The public surface ABI, per-process NX mappings, read-only non-transferable IPC
clones, sender/receiver exit ordering, failure rollback, restricted permission
profile, and QEMU resource accounting passed with zero drift. P4-R03 was
certified on 2026-07-15 at implementation commit `23ee0f0`. Its bounded
display protocol/backend tests, restricted-client
denial, real full/partial pixel presentation, terminal fallback, forced
display-service crash, automatic restart, resubmission, and transient resource
cleanup passed both focused QEMU testing and the 498.06-second full closure.
P4-R04 was certified on 2026-07-15 at implementation commit `231f27e`. Its
bounded one-client protocol, exact ownership/generation checks, supervised
window/display chain, restricted direct-display denial, deterministic pixels,
normal lifecycle, unexpected owner-exit cleanup, and stable resource samples
passed focused host/QEMU testing and the 515.74-second full closure. P4-R05
was certified on 2026-07-17 at implementation commit `4e0e296`. Its 12-slot
generation/ownership model, stable z-order, chunked damage validation,
four-edge clipping, deterministic host hash, QEMU overlap/hide/show/move/
resize pixels, arbitrary destruction order, and stable active-resource samples
passed focused testing and the 538.32-second full closure. P4-R06 was certified
on 2026-07-17 at implementation commit `76cb52e`, covering exact raw-input
authority, ordered focus transitions, focused-only keyboard routing, stale
identity rejection, and stable active resources. P4-R07 was certified on
2026-07-17 at implementation commit `4bafe1f`. The public SDK ABI, correlated
transport, mapped canvas, restricted first GUI application, F1 redraw, F2
resize, Escape teardown, temporal focus boundary, pixels, and active resource
stability passed focused host and QEMU testing. P4-R08 through P4-R12 were
certified on 2026-07-20 by commits `31b681e` and `8d44929`. Active-session
`displayd` and `windowd` crash injection restored the current console,
performed bounded automatic restart, rejected stale identities, and reacquired
new GUI-session generations with stable active resources. `make test-phase4`
passed in 216.5 seconds. Its 60-second GUI soak completed 35 application
cycles, 140 window lifecycles, and seven service-stack restarts with exact
warmed/final resources. A clean parallel UEFI build and the full
`make test-closure` matrix passed in 651.30 seconds, including SDK 91/91 and a
separate 42-cycle service soak. The optional one-hour release soak remains
separate from ordinary Phase 4 closure.
