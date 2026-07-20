# Phase 4.5 Progress

This is the live implementation ledger for the threading foundation. Stable
design belongs in [implementation_plan.md](implementation_plan.md); required
coverage belongs in [regression_matrix.md](regression_matrix.md). This file
records only work that has actually started or completed.

## Status Definitions

- `Planned`: no implementation claim has been made;
- `In progress`: implementation has started but the exit gate has not passed;
- `Blocked`: a named unmet dependency prevents useful progress;
- `Complete`: every exit-gate requirement passed and an evidence record exists.

A documentation plan is not implementation completion. `Complete` requires an
implementation commit, exact verification commands and results, resource
measurements where applicable, and a separate evidence commit that records the
immutable implementation hash.

## Current Status

| Subphase | Status | Started | Completed | Implementation commit(s) | Evidence |
| --- | --- | --- | --- | --- | --- |
| 4.5A: Thread object model | Planned | - | - | - | - |
| 4.5B: Main thread extraction | Planned | - | - | - | - |
| 4.5C: Thread ABI and SDK | Planned | - | - | - | - |
| 4.5D: Thread-aware waiting | Planned | - | - | - | - |
| 4.5E: Synchronization primitives | Planned | - | - | - | - |
| 4.5F: TLS, accounting, and fairness | Planned | - | - | - | - |
| 4.5G: Fault injection, soak, and closure | Planned | - | - | - | - |

Current status: Phase 4.5 planning is recorded and implementation has not
started. The next task is 4.5A. Phase 4.6 and Phase 5 remain gated on Phase 4.5
closure.

## Recording Workflow

For each subphase:

1. set the row to `In progress` when code or tests begin;
2. implement the bounded behavior and focused positive/negative tests;
3. run the focused exit-gate commands and all affected earlier suites;
4. commit implementation and tests;
5. add a dated record below using that immutable implementation commit;
6. change the row and matching roadmap item to `Complete` only after every
   required result passes;
7. commit the evidence update separately.

The evidence commit is separate because a Git commit cannot contain its own
final hash. Do not record a predicted hash.

## Evidence Record Format

Append one section per completed subphase:

```text
## 4.5X: Subphase Name

Status: Complete
Started: YYYY-MM-DD
Completed: YYYY-MM-DD
Implementation commits: `<immutable commit hashes>`

### Delivered

- concrete object, ABI, scheduler, wait, or synchronization result;
- compatibility and failure-policy result;
- diagnostics and tests delivered.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make <real-focused-target>` | PASS | counts, timing, or QEMU markers |
| `make <affected-regression>` | PASS | exact result |

### Resource Accounting

- warmed baseline: thread records/stacks/pages/handles/waits/objects/processes;
- final sample: same tuple and sampling boundary;
- unexplained drift: zero or a documented blocker.

### Remaining

- work assigned to the next subphase;
- accepted limitation already named in the plan.
```

## Phase 4 Entry Evidence

Phase 4 is the immutable starting point, not Phase 4.5 completion evidence.
It closed on 2026-07-20 with implementation commits `31b681e` and `8d44929`,
documentation commit `064e835`, and tag `phase-4-complete`. The aggregate Phase
4 test and full closure passed, including the required 60-second GUI soak with
zero warmed/final resource drift.

## Open Evidence Records

None. Add the first record only after 4.5A implementation and its exit-gate
tests pass.
