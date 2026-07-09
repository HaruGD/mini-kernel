# Kernel Context And Concurrency Rules

This document defines the execution-context and shared-state rules at the start
of Phase 3.5. The current kernel is single CPU, but interrupt handlers can
preempt ordinary kernel work. Accidental single-CPU atomicity is not a valid
long-term synchronization design.

## Execution Contexts

| Context | Interrupt state | May block | May allocate | May access VFS | May copy user memory |
| --- | --- | --- | --- | --- | --- |
| Early boot | disabled until IDT/controller setup completes | no | only initialized allocators | initialization only | no |
| Process/syscall | normally enabled | only through scheduler wait APIs | yes | yes | through validated copy helpers |
| Hard IRQ | disabled by interrupt-gate entry | no | no | no | no |
| User fault | disabled on entry | no | no | no | no |
| Kernel fault/panic | disabled permanently | no | no | no | no |

The current IPC and input wait loops manipulate IF directly to avoid a lost
wakeup between checking a queue and halting. Phase 3.5B must encapsulate this
inside the common wait core; subsystem code must not grow new `cli/sti/hlt`
loops.

## Hard IRQ Rules

Hard IRQ handlers may:

- acknowledge or mask their interrupt source
- read or write bounded device registers
- update bounded counters
- push a preallocated input or event record
- update timer and scheduler accounting
- request a preemption decision for interrupt return
- invoke an IRQ driver hook that obeys these same rules

Hard IRQ handlers must not:

- sleep, yield, wait, or launch a process
- allocate or free heap/physical pages
- perform VFS operations
- register or unregister services
- load, unload, or mutate driver tables
- copy to or from user pointers
- perform unbounded terminal, serial, or log output
- hold a lock across EOI or interrupt return

Emergency fault and first-occurrence warning output are current diagnostic
exceptions. They must not become normal IRQ data paths.

## Scheduler And Fault Rules

- Timer IRQ updates bounded accounting and wake deadlines only.
- A timer IRQ may request preemption, but context selection occurs through the
  established interrupt-return/kernel path.
- User faults record terminal failure and return to a safe kernel continuation.
- Kernel page faults, GP faults, and double faults enter panic and never resume.
- Fault cleanup must not wait for another process or allocate memory.

## Current Shared-State Model

There are no general spinlocks yet. Current safety relies on:

- one logical CPU
- interrupt-gate entry clearing IF
- short explicit interrupt-disabled check/sleep sections
- bounded ring operations
- no concurrent kernel threads

This is sufficient only for the current baseline. It is not SMP-safe and does
not permit arbitrary subsystem mutation from IRQ context.

Shared structures with both IRQ and process-context access include:

- scheduler/process state touched by the timer IRQ
- input queues touched by keyboard IRQ and syscall readers
- driver IRQ hook counters

IPC mailboxes and the service registry currently run from syscall/process
context, but future multi-threading will require explicit protection.

## Lock Contract For Future Work

Every introduced lock must declare:

- protected fields
- allowed execution contexts
- whether acquiring it disables local interrupts
- its position in the lock order
- whether diagnostic reads require the lock

Until a complete hierarchy is implemented, nested subsystem locks are
prohibited. If an operation needs two protected subsystems, it must split the
operation or define and test a strict order before merging.

The initial lock classes are:

1. scheduler/process state
2. address-space mappings
3. per-process handle table
4. IPC mailbox or service registry
5. VFS or device state

Locks are acquired only in increasing class order and released in reverse
order. A lock class may not call back into an earlier class.

## Blocking And Allocation Rules

- No code may sleep or wait while holding a spinlock.
- No hard IRQ path may take a lock that can be held across blocking work.
- User copy and VFS access occur without spinlocks held.
- General allocation occurs without spinlocks held unless an allocator
  explicitly provides an IRQ-safe, nonblocking pool.
- Wakeup records the result and makes a waiter ready before releasing the
  protecting lock.
- Cleanup must detach owned objects under protection, then perform slow
  destruction after releasing the lock.

## Interrupt-State Rules

- A function that disables interrupts must restore the caller's previous IF
  state, not blindly enable interrupts.
- Raw `cli`, `sti`, and `hlt` are restricted to architecture, scheduler,
  panic, and early-boot code.
- `sti; hlt` must be paired with a condition check protected against lost
  wakeups.
- Interrupt state is CPU-local and is not a substitute for an SMP lock.

## Review Checklist

Before accepting a new kernel path:

- Is its execution context explicit?
- Can an IRQ enter while it mutates shared state?
- Can it block, allocate, invoke VFS, or copy user memory?
- Does it call a function with a stronger context requirement?
- Is cleanup safe if the operation fails halfway?
- Are counters and diagnostics coherent under interruption?
- Would the design remain valid after a second CPU is introduced?
