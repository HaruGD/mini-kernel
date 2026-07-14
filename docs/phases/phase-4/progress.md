# Phase 4 Progress

This is the live implementation ledger for Phase 4. The frozen architecture
belongs in `compositor_contracts.md`, and planned work belongs in
`implementation_plan.md`. This file records only work that has actually
started or completed and the reproducible evidence for that state.

## Status Definitions

- `Planned`: no implementation claim has been made;
- `In progress`: implementation has started but the subphase exit gate has not
  passed;
- `Blocked`: a named unmet dependency prevents useful progress;
- `Complete`: every exit-gate requirement has passed and an evidence record is
  present below.

A checkbox or status label alone is not completion evidence. `Complete`
requires an implementation commit, exact verification commands and results,
measured resource results where applicable, and a separate evidence commit
that records those immutable implementation commit hashes.

## Current Status

| Subphase | Status | Started | Completed | Implementation commit(s) | Evidence |
| --- | --- | --- | --- | --- | --- |
| 4A: Page-backed surface foundation | Planned | - | - | - | - |
| 4B: Surface ABI, mapping, and transfer rights | Planned | - | - | - | - |
| 4C: Display-service present path | Planned | - | - | - | - |
| 4D: Single-window bring-up | Planned | - | - | - | - |
| 4E: Multiwindow compositor | Planned | - | - | - | - |
| 4F: Input routing and focus | Planned | - | - | - | - |
| 4G: Window SDK and first GUI application | Planned | - | - | - | - |
| 4H: Lifecycle, fault, regression, and closure | Planned | - | - | - | - |

Next planned work: Phase 4A, page-backed surface foundation.

## Recording Workflow

For each subphase:

1. change its status to `In progress` when implementation begins;
2. implement code and focused tests without weakening an earlier boundary;
3. run the subphase exit-gate commands from a clean, known commit;
4. commit the implementation and tests;
5. add the dated evidence record below, referencing the implementation commit
   or commits and the exact test results;
6. update the status table and roadmap checkbox to `Complete` only after every
   exit-gate condition passes;
7. commit the documentation as a separate subphase evidence/closure commit.

The evidence commit is separate because a Git commit cannot include its own
final hash in its contents. Do not insert a predicted or abbreviated hash
before the implementation commit exists.

## Evidence Record Format

Append one record per completed subphase using this format:

```text
## 4X: Subphase Name

Status: Complete
Started: YYYY-MM-DD
Completed: YYYY-MM-DD
Implementation commits: `<full or unambiguous commit hashes>`

### Delivered

- concrete externally observable implementation result;
- ABI, ownership, cleanup, or service-policy result;
- tests and diagnostics added with the implementation.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make <focused-target>` | PASS | relevant counts/timing |
| `make <qemu-target>` | PASS | visible or serial marker |

### Resource Accounting

- warmed baseline: handles/pages/regions/objects/processes;
- final sample: handles/pages/regions/objects/processes;
- unexplained drift: zero or a documented blocker.

### Remaining

- work explicitly assigned to the next subphase;
- accepted limitation already listed in the implementation plan.
```

Do not use a completion record to hide partial failures. A failed required test
leaves the subphase `In progress`, with the failure summarized in the working
notes or issue tracker until corrected.

## Phase Milestones

The progress ledger is intentionally more detailed than project history. Add a
history page and annotated Git tag only for durable milestones such as the
first window, first complete GUI application, and Phase 4 closure. Routine
subphase completion remains recorded here and in Git commits.
