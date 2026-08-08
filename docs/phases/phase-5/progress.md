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
| 5C: Graphics, fonts, images, and widget toolkit | Complete | 2026-08-02 | 2026-08-04 | `a13b5a8`, `ba15722` | P5-R05, P5-R06 |
| 5D: GUI terminal | Complete | 2026-08-04 | 2026-08-04 | `c78b6fc` | P5-R07 |
| 5S: System-call modernization | In progress | 2026-08-08 | - | `4e5dfa8`, `4244d82`, `1b1fb2c`, `719ca12`, `888df73` | 5S-A through 5S-E complete; P5S-R01 complete, P5S-R02/P5S-R03 in progress |
| Memory scalability gate | Planned | - | - | - | P5-R09 |
| 5E: Desktop shell | Planned | - | - | - | P5-R08 |
| 5F: File manager | Planned | - | - | - | P5-R10, P5-R11 |
| 5G: Settings and system UI | Planned | - | - | - | P5-R12 |
| 5H: Installed system layout | Planned | - | - | - | P5-R13 |
| 5I: Fault injection, soak, regression, and closure | Planned | - | - | - | P5-R14, P5-R15 |

Current status: 5A through 5D are complete. Phase 5S is in progress: 5S-A
through 5S-E are complete, while 5S-F compatibility migration is next. Phase 5S
must close before Phase 5E. The independent memory-scalability gate remains
required before Phase 5F.

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

### 5C: Graphics, Fonts, Images, And Widget Toolkit

- `make test-phase5c` passed native `.osimg`, BMP, and PNG positive and
  malformed fixtures; deterministic conversion; alpha/scaling pixels;
  clipping; UTF-8 replacement; layout; damage overflow; and widget dispatch.
- A restricted public-SDK application loaded all three formats in QEMU,
  produced the expected presentation pixel `(60, 180, 30)`, operated every
  baseline widget, and released its process, surface, mapping, and handles.
- `make test-user-sdk`, `make test-window-sdk`, and `make test-window-input`
  passed as affected inherited regression.
- 5C.1 added distinct `a-z` and printable ASCII punctuation glyphs and a
  visible focused-text-field caret without changing stored lowercase input.
- The public Window SDK now tracks server-authoritative frame geometry and
  performs bounded allocate-before-publish surface replacement. A live resize
  race re-queries the newest geometry while allocation failure preserves the
  old published surface and mapping.
- The public UI and image APIs support bounded rectangle replacement and
  centered aspect-preserving image fitting. The sample application recomputes
  every widget and image rectangle, repaints, and damages the new client area.
- Host tests cover glyph distinction, punctuation, caret pixels, overflow,
  aspect fitting, stale/duplicate configure coalescing, allocation failure,
  and configure/publication races.
- `make test-phase5c` passed its complete host and QEMU gate. QEMU dragged the
  client from `600x420` to `700x480`, observed an exact `700x480` replacement
  surface, verified the resized BMP pixel `(60, 180, 30)`, preserved lowercase
  text, operated all baseline widgets, and finished with stable active
  resources: baseline `(4, 21, 1, 0, 4, 0, 1, 107889, 4122016, 4165632)` and
  final `(4, 21, 1, 0, 4, 0, 1, 107860, 4122016, 4165632)`.
- Affected inherited regression also passed: `make test-window-sdk`,
  `make test-pointer-routing`, and `make test-user-sdk` (`91/91`).

The current GOP logical display remains fixed at its selected boot mode. QEMU
viewport zoom is only presentation scaling. Resolution-aware desktop layout
belongs to 5E, persistent supported-mode selection to 5G, and true runtime
mode switching to a future mode-setting display backend.

### 5D: GUI Terminal

- User SDK 2.5 freezes a 96-byte generation-bound terminal packet ABI for
  hello, output, input, resize, hangup, and exit-status messages.
- The bounded cell model supports 20-120 columns, 5-60 visible rows, 128 rows
  of fixed history, a visible cursor, tab/backspace/newline, scrollback, and
  baseline ANSI SGR, cursor, clear, erase, and save/restore controls.
- `terminal.elf` is a restricted Window SDK application. It receives keyboard
  input only through its focused window, publishes replacement surfaces on
  configure, recomputes its grid, and sends resize notifications to the child.
- `terminal_shell.elf` reuses the existing C shell command engine through an
  inheritable kernel terminal session. A dynamically allocated 256-packet
  output stream is separate from general IPC; input uses a bounded 256-byte
  ring and wakes only the newest active descendant.
- Standard writes, character input, clear-screen, kernel-backed file commands,
  FAT32 directory listing, and command diagnostics now honor the session.
  External ELF output and interactive input therefore remain inside the GUI
  terminal instead of leaking to or blocking on the kernel console.
- Normal shell exit, `HANGUP`, an unresponsive child timeout, and injected
  child loss converge on one cleanup path. The owned child is reaped once and
  is killed only if it fails the bounded hangup deadline.
- `make test-phase5d` passed host packet/model/ANSI/scrollback coverage and a
  QEMU run containing two normal command sessions, a live resize from
  `720x440` to an exact `780x480` surface and `120x57` grid, a clean hangup,
  and test-only injected child loss. Both normal sessions exercised `echo`,
  FAT32 `ls`, `save`, `cat`, external `uargs_c.elf` output, and inherited
  interactive `uinfo_c.elf` input entirely through the GUI stream. Both
  sessions rendered more than `8,000` visible non-background pixels.
- QEMU active-resource and heap values returned to baseline after the console
  regression and all four GUI lifecycles: baseline
  `(4, 21, 1, 0, 4, 0, 1, 107879, 4122016, 4165632)` and final
  `(4, 21, 1, 0, 4, 0, 1, 107811, 4122016, 4165632)`. Lock-order,
  recursion, and release violation counters remained zero.
- Affected inherited regression passed: `make test-abi-freeze`,
  `make test-phase5c`, `make test-process-lifecycle`, `make test-ipc`,
  `make test-user-sdk` (`91/91`), and `make test-window-sdk`.
- Post-completion hardening restores console input focus to a foreground
  parent after an external command exits. The QEMU gate now starts the window
  service, enters the console user shell, runs `bootinfo`, verifies the next
  prompt remains interactive, and returns to the kernel shell.
- The terminal uses a distinct bottom safety inset for both viewport rendering
  and resize row negotiation. At `720x440` it exposes `117x52`; after the live
  resize to `780x480` it exposes `120x57`, keeping the cursor clear of the
  client-area boundary.

### 5S-A: Catalog, Schema, And Generated ABI

- Implementation commit: `4e5dfa8`.
- `config/abi/syscalls.json` is the versioned source of truth for all active
  syscall numbers `1..111`, 18 explicit result codes, ABI registers, handler
  symbols, SDK aliases, argument direction and sizing, output state,
  permission, execution-context, resource, and reference fields.
- The deterministic generator emits seven tracked kernel, SDK, NASM,
  descriptor, result, and reference artifacts. Kernel and user sources no
  longer maintain separate numeric syscall or result-code lists.
- `make test-syscall-contract` passed catalog/schema validation, exact active
  range and dispatcher coverage, generated-artifact freshness and
  determinism, C/C++/NASM compilation, raw-number rejection, and negative
  mutations for duplicate keys/numbers, missing pointer sizing, and unknown
  error sets. Measured catalog coverage was 111 calls, 18 result codes, and 53
  calls with pointer-like arguments.
- `make test-abi-freeze`, `make test-kernel-language-contract`,
  `make test-user-sdk` (`91/91`), `make test-uefi-smoke`, and
  `make test-phase5d` passed as affected inherited regression. The Phase 5D
  QEMU run retained its established final active-resource shape and exercised
  console `bootinfo`, normal terminal use, hangup, and injected child loss.
- Every syscall entry intentionally remains marked `provisional`. P5S-R01
  closes structural catalog/generation coverage only; it does not claim the
  per-call semantic audit in 5S-G or the `SYSCALL/SYSRET` entry migration in
  5S-E/F.

### 5S-B: Result And Output Contract

- Implementation commit: `4244d82`.
- Catalog/schema version 2 defines a signed 64-bit public result domain,
  reserves `-4095..-1` for cataloged errors, records `zero`, `nonnegative`,
  `positive`, or `noreturn` success per call, and keeps scheduler control
  tokens outside the public error range.
- The stable result table grew additively from 18 to 22 codes with distinct
  invalid, stale, and wrong-type handle failures plus checked arithmetic
  overflow. Generated kernel/SDK headers and SDK strings share the same source
  and preserve the existing `-1..-18` assignments.
- All 111 entries now carry machine-checked output publication. Measured
  coverage is 85 calls without output pointers, 25 atomic outputs that remain
  unchanged on failure, and one documented partial-output call:
  `SYS_IPC_QUERY` on `OS_ERR_BAD_BUFFER`, retried as a whole.
- The common handle resolver returns exact malformed-token, generation,
  object-type, and rights failures without modifying its output snapshot.
  VFS handle calls and surface/general object inspection propagate those
  results. Ambiguous raw numeric `-1` returns were removed from syscall
  dispatcher sources, and unknown numbers now return `OS_ERR_UNSUPPORTED`.
- `make test-syscall-contract`, `make test-abi-freeze`, and
  `make test-kernel-handles` passed structured-result/output mutation tests,
  generated SDK message execution, internal-token separation, exact handle
  errors, and unchanged failure outputs.
- `make test-user-sdk` passed `97/97` in QEMU, including unknown-call,
  generated-message, malformed/stale/wrong-type handle, atomic-output, and
  documented partial-output cases. Affected inherited regression passed:
  `make test-kernel-language-contract` (text `215,554`, data `416`, BSS
  `1,630,752`, two initializers, five vtables),
  `make test-process-lifecycle`, `make test-window-sdk`, and
  `make test-phase5d`.
- The Window SDK QEMU run retained resources from baseline
  `(4, 21, 1, 0, 4, 0, 1, 108966, 1946016, 1986560)` to final
  `(4, 21, 1, 0, 4, 0, 1, 108940, 1946016, 1986560)`. The GUI terminal run
  retained its established baseline
  `(4, 21, 1, 0, 4, 0, 1, 107879, 4122016, 4165632)` and final
  `(4, 21, 1, 0, 4, 0, 1, 107811, 4122016, 4165632)` values.
- Entries remain `provisional`; 5S-B closes the shared result and output model,
  not the common user-copy implementation in 5S-C or complete per-call audit
  in 5S-G.

### 5S-C: User-Memory Validation And Copy Boundary

- Implementation commit: `1b1fb2c`.
- Catalog/schema version 3 adds mandatory power-of-two alignment, read/write
  access, snapshot/publication, encoding, and nested-buffer policy to every
  argument. Generated kernel descriptors now carry pointer, readable,
  writable, snapshot, nested, and three-argument alignment metadata. The
  catalog remains complete at 111 calls, 22 result codes, 53 pointer-bearing
  calls, 25 atomic-output calls, and one partial-output call.
- The common boundary returns explicit checked-arithmetic, canonical-address,
  range, mapping, region/PTE permission, alignment, empty-range, bounded
  string, array, versioned-structure, and UTF-8 results. Existing copy helpers
  now route through it, while `SYS_WRITE` propagates exact bad-buffer and
  overflow results instead of collapsing them into invalid argument.
- A short-lived mapping lease blocks map, unmap, protection, region, and reset
  mutation after validation and before the last byte copy. The mutation side
  waits without holding the address-space spinlock. Copy start is rejected
  while a spinlock or TLB-wait context is active, and input metadata remains a
  kernel-owned snapshot rather than a live user pointer.
- `make test-syscall-validation` passed checked addition/multiplication,
  canonical and misaligned addresses, zero-range policy, cross-page input,
  unmapped/read-only failures, unchanged failed output, page-boundary strings,
  version rejection, UTF-8, mutation-gate refusal, and a real threaded unmap
  held until lease release.
- `make test-syscall-contract`, `make test-abi-freeze`, and the complete
  `make -j4 all64` build passed generated-artifact freshness, negative schema
  mutations, C/C++/NASM descriptor compilation, and ABI version 3.
- The User SDK QEMU suite passed `101/101` on both one and four vCPUs. New live
  cases rejected a kernel pointer, an unmapped user hole, a wrapping range, and
  a read-only output without publication, then accepted a mapped input that
  crossed a page boundary.
- Affected memory/concurrency regression passed: `make test-surface-abi`,
  `make test-tlb-shootdown`, and `make test-tlb-lock-order`. The 64-cycle SMP
  shootdown run completed `13,508,100` concurrent reads with AP acknowledgement
  counts `141/137/137`; the surface mapping smoke returned exactly to baseline
  `(0, 0, 0, 0, 0, 0, 0, 108887, 4122016, 4165632)`.
- `make test-kernel-language-contract` passed at text `217,552`, data `416`,
  BSS `1,630,752`, two initializers, and five vtables. `make
  test-process-lifecycle` and `make test-phase5d` also passed; the GUI terminal
  retained its established baseline
  `(4, 21, 1, 0, 4, 0, 1, 107879, 4122016, 4165632)` and final
  `(4, 21, 1, 0, 4, 0, 1, 107811, 4122016, 4165632)` values.
- 5S-C is complete, but P5S-R03 intentionally remains in progress. Generic
  descriptor permission preflight closes in 5S-D; fault-injected copy and
  allocation cleanup plus the complete per-call audit remain in 5S-G/H.

### 5S-D: Dispatch, Permissions, And Auditability

- Implementation commit: `719ca12`.
- Schema/catalog version 4 gives every syscall a structured authority mode and
  generated permission mask. The descriptor also carries nullability and
  direct `argN bytes` range relationships without introducing a second policy
  list in the dispatcher.
- The active entry path reaches one common dispatcher for exact number lookup,
  active process/thread and owner-generation checks, corrupt permission-state
  rejection, required-permission preflight, pointer null/canonical/alignment/
  overflow/base-access checks, and handler routing. Permission denial occurs
  before pointer inspection. Subsystem owner/session and exact handle checks
  remain defense-in-depth in their owning handlers.
- The sole partial-output call, `SYS_IPC_QUERY`, retains handler-ordered
  publication. Empty `write` and VFS read/write buffers preserve their
  zero-byte null-buffer behavior; wrapping byte ranges retain
  `OS_ERR_OVERFLOW`.
- Atomic diagnostics count total, dispatched, rejected, stable rejection
  reasons, and the last rejected number/reason. The `syscalls` shell command
  exposes those counters without recording user payloads, strings, addresses,
  object contents, or secrets.
- `make test-syscall-contract`, `make test-syscall-validation`,
  `make test-abi-freeze`, `make -j4 all64`, and `make uefi` passed. The catalog
  measured 111 calls, 22 result codes, 53 pointer-bearing calls, 25 atomic
  outputs, and one partial output.
- The User SDK QEMU suite passed `102/102` on both one and four vCPUs. A child
  with zero permissions proved permission-before-pointer rejection for IPC,
  service discovery, input, shared surfaces, display, and child management,
  while identity and time remained available.
- Affected host regression passed for kernel language/toolchain (text
  `230,096`, data `416`, BSS `1,630,752`, two initializers, five vtables),
  process lifecycle, handles, IPC, services, display, multiwindow, and Window
  SDK contracts. QEMU regression passed IPC, surface mapping, all service
  supervision smokes, and the GUI terminal lifecycle.
- Surface mapping returned exactly to baseline/final
  `(0, 0, 0, 0, 0, 0, 0, 108887, 4122016, 4165632)`. GUI terminal resources
  remained at the established baseline
  `(4, 21, 1, 0, 4, 0, 1, 107879, 4122016, 4165632)` and final
  `(4, 21, 1, 0, 4, 0, 1, 107811, 4122016, 4165632)` values.
- 5S-D is complete. P5S-R03 remains in progress until 5S-G/H add the complete
  scalar/flag/handle audit and injected copy/allocation failure matrix; 5S-E/F
  still own `SYSCALL/SYSRET` and compatibility migration.

### 5S-E: x86_64 `SYSCALL/SYSRET` Entry

- Implementation commit: `888df73`.
- The BSP and every AP verify architectural support, program and read back
  `IA32_EFER.SCE`, `IA32_STAR`, `IA32_LSTAR`, and
  `IA32_FMASK=0x44700`, and publish readiness only after an aligned current
  kernel entry stack exists. AP publication fails closed before `ONLINE`.
- Kernel/user GDT selectors now satisfy the architectural STAR relationships:
  kernel `0x08/0x10`, user `0x2b/0x23`. The scheduler updates TSS `RSP0` and
  fast-entry stack ownership together.
- The entry path saves the untrusted user RSP before switching stacks,
  preserves every logical frame register including temporary `R10`, clears
  DF, and constructs the same 20-word frame consumed by existing yield,
  sleep, wait, exit, fault, and resume paths. Both transports call the sole
  descriptor dispatcher and expose the same results and permissions.
- Return validation rechecks process/thread generations, ownership, loaded
  address-space pointer/identity/root, selectors, RFLAGS, canonical RIP/RSP,
  executable RIP, and writable stack. Safe state uses `SYSRETQ`; DF and other
  slow flags use `IRETQ`; invalid return state terminates only the caller as
  GP fault status `0x5e01`.
- A latent 5S-D SMP lifecycle error was corrected: `Process::state` is a
  compatibility summary that may be `PAUSED` while an owned sibling thread is
  executing on another CPU. Common preflight and fast-entry validation now
  accept `RUNNING` or `PAUSED` active processes while continuing to reject
  terminal and exiting state. The host dispatcher test covers this rule.
- `make test-syscall-entry`, `make test-syscall-validation`,
  `make test-syscall-contract`, and `make test-abi-freeze` passed. The focused
  entry test covers normal/slow/abort state transitions, executable and
  writable return mappings, identity/address-space mutation, and assembly
  source contracts.
- The User SDK QEMU suite passed `104/104` on one and four vCPUs while mixing
  `int 0x80` with raw `SYSCALL`. Both runs measured eight fast entries, five
  `SYSRETQ` returns, one deliberate DF `IRETQ` fallback, one isolated invalid
  return abort, and one matching invariant-failure count. Ready CPU counts
  were exactly `1/1` and `4/4`.
- Existing thread creation/join regression completed 18 repeated lifecycles
  with unchanged resource and scheduler baselines; thread wait/wake passed.
  The inherited four-CPU execution workload passed with CPU mask `0x0e`,
  three simultaneous workers, 34 preemptions, and zero workload failures.
- 5S-E is complete. P5S-R02 remains in progress until 5S-F makes the verified
  transport the SDK default and records the final bounded `int 0x80`
  compatibility decision.

### Cross-Phase Kernel Language And Toolchain Hardening

- The kernel language contract now fixes GNU C++17 as a restricted
  freestanding implementation profile while preserving C ABI boundaries for
  syscalls, drivers, firmware, and assembly.
- `KERNEL_OPT` selects one unambiguous optimization profile. Clean default
  `-Os` and alternate `-O2` kernels both built and passed the binary contract.
- The avoidable shell scalar dynamic initializer and the unlinked duplicate
  C++ runtime source were removed. The remaining two boot-lifetime global
  initializers and five legacy driver vtables are explicitly allowlisted.
- `make test-kernel-language-contract` rejects forbidden language/runtime
  features, sections, symbols, initializers, vtables, duplicate runtime code,
  optimization bypass, and text/data/BSS budget overflow. It is included in
  the inherited `test-closure` gate.
- Clean `-Os` evidence: text `215,202`, data `416`, BSS `1,630,752` bytes,
  two initializers, five vtables. Clean `-O2` evidence: text `280,124`, data
  `416`, BSS `1,630,752` bytes, with the same initializer and vtable counts.
- Affected regression passed: `make test-abi-freeze`, the driver build and
  regression matrix checks, `make test-uefi-smoke`, and `make test-phase5d`.
  The GUI terminal retained stable active resources across its console
  `bootinfo`, normal, hangup, and injected-fault lifecycles.

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
5A -> 5B -> 5C -> 5C.1 -> 5D -> 5S -> 5E -> 5F -> 5G -> 5H -> 5I
                                         |
                                         `-- memory scalability must close before 5F
```

## Phase 5S Planning Record

- Date: 2026-08-08
- Phase 5S is inserted as the mandatory next gate between completed 5D and 5E.
- Its catalog, generated ABI, explicit result and user-memory contracts,
  `SYSCALL/SYSRET` migration, complete call audit, and required regression are
  defined in
  [syscall_modernization_plan.md](syscall_modernization_plan.md).
- This was a planning-only record when written. 5S-A through 5S-E subsequently
  completed their focused gates in `4e5dfa8`, `4244d82`, `1b1fb2c`, and
  `719ca12`, with the architecture-entry implementation in `888df73`.
  P5S-R01 is complete; 5S-F through 5S-H and P5S-R02 through P5S-R04 still
  require their remaining measured exit evidence.

## Planning Record

- Date: 2026-08-01
- Phase 5 purpose, authority boundaries, subphases, memory gate, negative
  coverage, and closure evidence are defined.
- No implementation commit or test result is claimed by this record.
