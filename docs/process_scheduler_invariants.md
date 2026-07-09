# Process And Scheduler Invariants

This document records the process and scheduler contract at the start of Phase
3.5. It describes the current implementation, including known transitional
behavior that later tasks must replace.

## Scope

The current kernel is single CPU and models one execution context per process.
A process owns its user mappings, saved register context, input queue, IPC
mailbox, current directory, and VFS/service ownership records.

Two related state machines exist:

- `ProcessState` describes image and execution lifecycle.
- `SchedulerState` describes whether the execution context may run.

They must be interpreted together. Neither state alone is sufficient.

## Process States

| State | Meaning | Entered by | Valid exits |
| --- | --- | --- | --- |
| `EMPTY` | Record has no process identity or owned runtime state | `process_clear` | `LOADED`, `FAILED` during attempted launch |
| `LOADED` | Record and image metadata exist; execution has not entered user mode | user loader | `RUNNING`, `FAILED` |
| `RUNNING` | User context is currently selected or entering user mode | launch/resume path | `PAUSED`, `RETURNED`, `FAILED` |
| `PAUSED` | A resumable register context was saved | yield, timer preemption, sleep | `RUNNING`, `FAILED` |
| `RETURNED` | Process completed normally and retains a waitable result | `process_mark_returned` | reaped, then `EMPTY` on record reuse |
| `FAILED` | Load, memory, mapping, fault, or forced-stop failure is retained | `process_mark_failed` | reaped, then `EMPTY` on record reuse |

`RETURNED` and `FAILED` are terminal. Code must not restore their user context.
Reaping marks a terminal record reusable but does not immediately erase its
result. `process_clear` performs final record reset when the slot is reused.

## Scheduler States

| State | Meaning | Entered by | Valid exits |
| --- | --- | --- | --- |
| `NONE` | Record is not owned by the scheduler | `process_clear` | `READY`, `FINISHED` on launch failure |
| `READY` | Resumable context is eligible for selection | enqueue, yield, preemption, timer wake | `RUNNING`, `WAITING`, `FINISHED` |
| `RUNNING` | Context is the current scheduled user execution | launch/resume | `READY`, `WAITING`, `FINISHED` |
| `WAITING` | Context is not eligible until an external condition changes | parent wait, timer sleep | `READY`, `RUNNING`, `FINISHED` |
| `FINISHED` | Context must never be scheduled again | normal return, failure, forced stop | `NONE` after record reuse |

The scheduler queue may contain only active records. A selectable record must
satisfy all of the following:

- nonzero pid
- `active != 0`
- `resumable != 0`
- `scheduler_state == SCHED_STATE_READY`
- a saved context that belongs to the same process record

## Valid Combined States

| Process state | Scheduler state | Valid use |
| --- | --- | --- |
| `EMPTY` | `NONE` | free record |
| `LOADED` | `NONE` or `READY` | launch construction |
| `RUNNING` | `RUNNING` | active user execution |
| `RUNNING` | `WAITING` | parent blocked while a child runs |
| `PAUSED` | `READY` | yielded or preempted context |
| `PAUSED` | `WAITING` | timer sleep |
| `RETURNED` | `FINISHED` | normal waitable result |
| `FAILED` | `FINISHED` | failed waitable result |

All other combinations require a documented transition or indicate partially
updated state.

## Transition Owners

- The loader owns `EMPTY -> LOADED`.
- Launch and resume paths own `LOADED/PAUSED -> RUNNING`.
- Interrupt/syscall context-save paths own `RUNNING -> PAUSED`.
- Scheduler helpers own scheduler-state transitions and queue membership.
- `process_mark_returned` owns every normal terminal transition.
- `process_mark_failed` owns every abnormal terminal transition.
- Reap helpers own only the `reaped` result flag.
- `process_clear` owns terminal record reset and final identity removal.

No new code may directly assemble a terminal process state. Phase 3.5C will
route all terminal paths through one cleanup operation.

## Owned Resource Invariants

For an active process:

- pid is nonzero and unique in the current process table.
- `slot_index` identifies its current user mapping slot.
- VFS handles, service registrations, input queue, and IPC mailbox are owned by
  that pid.
- `heap_break` stays between `heap_base` and `heap_limit`.
- only the focused process receives per-process input events.

For a terminal process:

- `active == 0`
- `resumable == 0`
- scheduler state is `FINISHED`
- VFS handles and service registrations are closed
- input and IPC wait flags are clear
- event queue and mailbox are reset
- result fields remain available until reaped and reused

## Current Wait Behavior

Timer sleep is represented as:

```text
ProcessState=PAUSED
SchedulerState=WAITING
pause_reason=SLEEP
wake_tick=<deadline>
```

Yield and preemption are represented as:

```text
ProcessState=PAUSED
SchedulerState=READY
pause_reason=YIELD or PREEMPT
```

IPC and input blocking currently differ. Their syscall handlers retain control
in kernel mode, use separate `ipc_waiting` or `input_waiting` flags, run other
ready processes cooperatively, and use an interrupt-safe `sti; hlt; cli`
check-and-sleep loop. They are not first-class scheduler waits yet.

This difference is a known Phase 3.5A baseline limitation. Phase 3.5B must
replace it with one wait-reason and block/wake contract.

## Required Assertions For Later Tasks

Phase 3.5 implementation should eventually enforce:

- a terminal process is never in the scheduler queue
- a waiting process is never selected as ready
- a saved context is resumed only from `PAUSED`
- one process has at most one active wait reason
- cleanup is idempotent
- wakeup changes a waiting process to ready at most once
- process-table reuse changes the process identity generation
