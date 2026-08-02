# Phase 5 Progress

This is the live implementation ledger for the desktop foundation. Stable
policy belongs in [implementation_plan.md](implementation_plan.md); required
coverage belongs in [regression_matrix.md](regression_matrix.md).

## Status Definitions

- `Planned`: no implementation claim has been made;
- `In progress`: implementation has started but its exit gate has not passed;
- `Blocked`: a named unmet dependency prevents useful progress;
- `Complete`: the subphase exit gate passed and immutable evidence exists.

Reserved test target names are plans, not executed evidence. Completion
requires implementation, focused positive and negative tests, affected
inherited regression, measured resource evidence, and a commit hash.

## Current Status

| Subphase | Status | Started | Completed | Implementation commit(s) | Evidence |
| --- | --- | --- | --- | --- | --- |
| 5A: Desktop session and layer policy | Complete | 2026-08-02 | 2026-08-02 | `88a16ea` | P5-R01, P5-R02 |
| 5B: Pointer and interactive windows | Complete | 2026-08-02 | 2026-08-02 | `be9fdff` | P5-R03, P5-R04 |
| 5C: Graphics, fonts, images, and widget toolkit | In progress | 2026-08-02 | - | `a13b5a8` (foundation) | P5-R05, P5-R06 |
| 5D: GUI terminal | Planned | - | - | - | P5-R07 |
| Memory scalability gate | Planned | - | - | - | P5-R09 |
| 5E: Desktop shell | Planned | - | - | - | P5-R08 |
| 5F: File manager | Planned | - | - | - | P5-R10, P5-R11 |
| 5G: Settings and system UI | Planned | - | - | - | P5-R12 |
| 5H: Installed system layout | Planned | - | - | - | P5-R13 |
| 5I: Fault injection, soak, regression, and closure | Planned | - | - | - | P5-R14, P5-R15 |

Current status: 5A and 5B are complete. The 5C foundation exists, but 5C.1
responsive rendering completion is active and blocks entry into 5D.

## Completed Evidence

### 5A: Desktop Session And Layer Policy

- `make test-phase5a` passed the session supervisor and layer authority gates.
- Ordered startup, idempotent requests, generation rollover, restart
  exhaustion, console fallback, clipping, overlap, and cleanup were checked.

### 5B: Pointer And Interactive Windows

- `make test-phase5b` passed host and QEMU pointer/interaction coverage.
- The inherited `make test-window-multi`, `make test-window-input`, and
  `make test-window-sdk` suites passed after the changes.
- QEMU exercised PS/2 motion, button delivery, focus, capture, title-bar drag,
  edge resize, controls, cursor ordering, and exact active-resource cleanup.

### 5C Foundation Evidence

- `make test-phase5c` passed native `.osimg`, BMP, and PNG positive and
  malformed fixtures; deterministic conversion; alpha/scaling pixels;
  clipping; UTF-8 replacement; layout; damage overflow; and widget dispatch.
- A restricted public-SDK application loaded all three formats in QEMU,
  produced the expected presentation pixel `(60, 180, 30)`, operated every
  baseline widget, and released its process, surface, mapping, and handles.
- `make test-user-sdk`, `make test-window-sdk`, and `make test-window-input`
  passed as affected inherited regression.

This evidence remains valid for the implemented foundation, but does not close
5C after interactive review found that lowercase input was stored correctly
and then rendered as uppercase, and that the sample application did not
replace its surface or recompute its layout after configure events.

## Active 5C.1 Completion Work

- add distinct lowercase and required ASCII punctuation glyphs;
- render a visible caret in the focused text field;
- handle configure events with allocate-before-publish surface replacement;
- recompute bounded widget/image layout for the new client size;
- preserve requested image aspect ratios;
- test minimum/overflow sizes, resize storms, allocation failure, stale
  configure events, and exact resource cleanup in host and QEMU paths.

The current GOP logical display remains fixed at its selected boot mode. QEMU
viewport zoom is only presentation scaling. Resolution-aware desktop layout
belongs to 5E, persistent supported-mode selection to 5G, and true runtime
mode switching to a future mode-setting display backend.

## Recording Workflow

For each subphase:

1. mark only the active subphase `In progress`;
2. implement bounded behavior and focused positive/negative tests;
3. run its focused gate and every affected inherited suite;
4. commit implementation and tests;
5. record the immutable implementation hash and measured evidence;
6. mark the row `Complete` only after every required result passes;
7. commit the evidence update separately when appropriate.

## Planned Order

```text
5A -> 5B -> 5C -> 5C.1 -> 5D -> 5E -> 5F -> 5G -> 5H -> 5I
                                  |
                                  `-- memory scalability must close before 5F
```

## Planning Record

- Date: 2026-08-01
- Phase 5 purpose, authority boundaries, subphases, memory gate, negative
  coverage, and closure evidence are defined.
- No implementation commit or test result is claimed by this record.
