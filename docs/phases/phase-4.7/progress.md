# Phase 4.7 Progress

This is the live implementation ledger for driver memory and DMA. Stable
policy belongs in [implementation_plan.md](implementation_plan.md); required
coverage belongs in [regression_matrix.md](regression_matrix.md).

## Status Definitions

- `Planned`: no implementation claim has been made;
- `In progress`: implementation has started but its exit gate has not passed;
- `Blocked`: a named unmet dependency prevents useful progress;
- `Complete`: the subphase exit gate passed and immutable evidence exists.

Reserved target names are plans, not executed evidence. Completion requires an
implementation commit, exact commands, resource measurements, negative
coverage, and a separate evidence commit.

## Current Status

| Subphase | Status | Started | Completed | Implementation commit(s) | Evidence |
| --- | --- | --- | --- | --- | --- |
| 4.7A: Ownership and lifetime contracts | Planned | - | - | - | P47-R01 |
| 4.7B: Reusable driver virtual address space | Planned | - | - | - | P47-R02, P47-R03 |
| 4.7C: Owned allocation and execution contexts | Planned | - | - | - | P47-R04, P47-R05 |
| 4.7D: Capability-scoped MMIO | Planned | - | - | - | P47-R06, P47-R07 |
| 4.7E: Coherent DMA and address model | Planned | - | - | - | P47-R08 |
| 4.7F: Streaming DMA, scatter/gather, and domain policy | Planned | - | - | - | P47-R09, P47-R10 |
| 4.7G: Quiescent unload and automatic cleanup | Planned | - | - | - | P47-R11 |
| 4.7H: Fault injection, device smoke, soak, and closure | Planned | - | - | - | P47-R12, P47-R13 |

Current status: Phase 4.7 is planned. Phase 4.6 remains the immutable entry
baseline.

## Implementation Order

```text
4.7A ownership/lifetime contracts
  -> 4.7B reusable driver VA
  -> 4.7C owned allocation/context rules
  -> 4.7D capability MMIO
  -> 4.7E coherent DMA/address model
  -> 4.7F streaming DMA/SG/domain policy
  -> 4.7G quiescent unload
  -> 4.7H fault/soak/closure
```

## Recording Workflow

For each subphase:

1. mark only that subphase `In progress`;
2. implement bounded behavior and focused positive/negative tests;
3. run the subphase gate and every affected inherited suite;
4. commit implementation and tests;
5. record the immutable implementation hash and measured evidence;
6. mark the row complete only when every required result passes;
7. commit evidence separately.

## Evidence Record Template

```text
## 4.7X: Name

- Status: Complete
- Started: YYYY-MM-DD
- Completed: YYYY-MM-DD
- Implementation commit(s): `<hashes>`

### Delivered

- concrete ownership, mapping, DMA, or unload result;
- failure and fallback policy;
- diagnostics and tests.

### Verification

| Command | Result | Measured evidence |
| --- | --- | --- |
| `make <real-target>` | PASS | handles, pages, VA ranges, DMA maps, cycles |

### Resource Accounting

- driver generation and device identity;
- owned allocations, pages, VA intervals, BAR maps, DMA maps, IRQs, and
  in-flight calls before/after;
- physical, virtual, and device address evidence without leaking addresses to
  ordinary user diagnostics;
- injected failure counts and unexplained drift;
- remaining deferred work.
```
