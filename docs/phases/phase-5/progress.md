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
| 5A: Desktop session and layer policy | Planned | - | - | - | P5-R01, P5-R02 |
| 5B: Pointer and interactive windows | Planned | - | - | - | P5-R03, P5-R04 |
| 5C: Graphics, fonts, images, and widget toolkit | Planned | - | - | - | P5-R05, P5-R06 |
| 5D: GUI terminal | Planned | - | - | - | P5-R07 |
| Memory scalability gate | Planned | - | - | - | P5-R09 |
| 5E: Desktop shell | Planned | - | - | - | P5-R08 |
| 5F: File manager | Planned | - | - | - | P5-R10, P5-R11 |
| 5G: Settings and system UI | Planned | - | - | - | P5-R12 |
| 5H: Installed system layout | Planned | - | - | - | P5-R13 |
| 5I: Fault injection, soak, regression, and closure | Planned | - | - | - | P5-R14, P5-R15 |

Current status: planning complete; implementation has not started. The Phase
4.7 closure baseline permits Phase 5A to begin.

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
5A -> 5B -> 5C -> 5D -> 5E -> 5F -> 5G -> 5H -> 5I
                         |
                         `-- memory scalability must close before 5F
```

## Planning Record

- Date: 2026-08-01
- Phase 5 purpose, authority boundaries, subphases, memory gate, negative
  coverage, and closure evidence are defined.
- No implementation commit or test result is claimed by this record.
