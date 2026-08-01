# Phase 5 Implementation Plan

## Goal

Build a recoverable native desktop from the existing surface, window, input,
service, thread, SMP, and driver foundations without moving desktop policy
into the kernel.

## Fixed Architecture Rules

- `displayd` owns scanout-facing presentation; applications never access GOP
  or a graphics driver directly.
- `inputd` owns physical input ingestion; `windowd` owns hit testing, focus,
  capture, and window interaction policy.
- `windowd` remains the window server and initial compositor during Phase 5.
- `sessiond` supervises the graphical user session and its restart boundary.
- `desktopd` is a privileged user-session client for desktop and panel layers,
  not a kernel component.
- terminal, file-manager, settings, and ordinary application windows use the
  public Window SDK.
- the kernel console remains available for boot, diagnostics, panic, and
  bounded graphical-session recovery.
- all tables, queues, damage lists, text buffers, retries, and restart loops
  have explicit limits and observable overflow policy.

## Implementation Order

```text
5A session and layer policy
  -> 5B pointer and interactive windows
  -> 5C graphics, fonts, and widget toolkit
  -> 5D GUI terminal
  -> 5E desktop shell
  -> 5F file manager
  -> 5G settings and system UI
  -> 5H installed-system layout
  -> 5I fault, soak, regression, and closure
```

The memory-scalability gate may progress beside 5A through 5E but must close
before 5F and the multi-application closure workload.

## 5A: Desktop Session And Layer Policy

Deliver:

- a bounded `sessiond` lifecycle with one initial local user session;
- explicit start order for `displayd`, `inputd`, `windowd`, and `desktopd`;
- `DESKTOP`, `NORMAL_WINDOW`, `PANEL`, and `SYSTEM_OVERLAY` policy;
- capability checks that deny desktop/panel/overlay layers to ordinary apps;
- idempotent start, stop, crash restart, and generation rollover;
- deterministic fallback to the preserved kernel console when restart policy
  is exhausted.

Exit gate: a persistent empty desktop session owns the display, ordinary
windows remain above the desktop and below the panel, stale session requests
fail, and every failure path either restarts cleanly or restores the console.

## 5B: Pointer And Interactive Windows

Deliver:

- an initial PS/2 mouse path with bounded motion, buttons, and wheel events;
- pointer coordinate accumulation and screen-edge clamping;
- a compositor cursor plane or equivalent topmost bounded cursor path;
- top-to-bottom hit testing with hidden, clipped, and destroyed-window rules;
- click focus, raise, pointer capture, and capture cancellation on teardown;
- server-authoritative title bars, close/minimize/maximize controls, drag, and
  edge/corner resize transactions;
- temporal focus/capture rules that prevent stale input crossing generations.

Exit gate: QEMU pointer input can manipulate overlapping windows without
duplicate delivery, out-of-bounds geometry, stuck capture, or resource drift.

USB HID remains part of the parallel xHCI hardware track.

## 5C: Graphics, Fonts, And Widget Toolkit

Deliver:

- bounded source-over alpha blending and clipping;
- a built-in bitmap font first, with UTF-8 decoding and replacement behavior
  but no initial complex-script shaping requirement;
- shared drawing primitives, theme values, and damage-aware repaint;
- labels, buttons, text fields, check boxes, lists, menus, scroll containers,
  and simple horizontal/vertical layout;
- keyboard focus traversal, pointer activation, disabled state, and predictable
  event dispatch;
- a public user library rather than application copies of private rendering
  code.

Exit gate: a restricted sample application builds only against public SDKs,
renders and operates every baseline widget, resizes, and exits with exact
surface, handle, heap, and process cleanup.

## 5D: GUI Terminal

Deliver:

- `terminal.elf` as an ordinary Window SDK application;
- a bounded PTY-like or terminal IPC byte stream between a terminal frontend
  and a child user shell;
- child spawn, exit status, resize notification, and hangup semantics;
- character cells, cursor, scrollback, newline/tab/backspace, and baseline ANSI
  color/control handling;
- input routing through the focused terminal window, never through direct
  kernel-console ownership;
- bounded output buffering and explicit overflow/backpressure behavior.

Exit gate: the graphical terminal launches the existing user shell, runs
commands, scrolls and resizes correctly, reaps the child exactly once, and can
be repeatedly opened and closed without process or memory drift.

## Memory Scalability Gate

Before Phase 5F begins:

- derive managed physical ranges from validated UEFI/E820 memory metadata;
- allocate or reserve PMM metadata according to the discovered range instead
  of `PMM_MAX_RAM_SIZE=512 MiB`;
- reserve the PMM metadata itself and support discontiguous usable ranges
  without allocating metadata from a range before it is described;
- preserve holes, firmware/runtime regions, MMIO exclusions, and all boot
  reserved ranges;
- audit the paging and kernel-mapping paths so a page above 4 GiB can be mapped
  without a 32-bit or identity-map assumption;
- validate memory above 4 GiB without confusing CPU physical and DMA-visible
  addresses;
- retain fail-closed invalid-map behavior, SMP-safe allocation, bounded
  contiguous-allocation failure, and exact statistics;
- test several QEMU RAM sizes and fragmented memory-map models.

NUMA, swap, compression, and huge pages are not required by this gate.

## 5E: Desktop Shell

Deliver:

- `desktopd` background and panel surfaces;
- application launcher and installed-application discovery;
- task list, active-window indication, activation, minimize, and restore;
- clock and bounded system status;
- keyboard window switching and secure, reserved global shortcuts;
- launch entries for the GUI terminal and later system applications.

Exit gate: the desktop persists while applications start, fail, restart, and
exit; `desktopd` recovery does not destroy ordinary application processes or
lose console fallback.

## 5F: File Manager

Deliver the required SDK/VFS contracts first:

- bounded directory enumeration and stable entry records;
- stat/type/size/time metadata;
- create directory, rename, remove, copy, and move operations;
- path normalization and explicit cross-filesystem behavior;
- file-open dispatch and application association lookup.

Then deliver `files.elf` with path navigation, list/icon presentation,
selection, file operations, progress/error dialogs, and user folders such as
Desktop, Documents, Downloads, and Pictures.

Exit gate: normal, empty, large, malformed, disappearing, read-only, and
failure-injected directories remain responsive and preserve filesystem and
process resources. Crash-consistent production storage remains a hardware and
filesystem responsibility, not a UI claim.

## 5G: Settings And System UI

Deliver:

- a versioned, bounded settings service and persistent serialization policy;
- theme/background, display, input, time, and system-information models;
- controlled service diagnostics and allowed configuration changes;
- shutdown and reboot requests routed through the existing power authority;
- reusable message, confirmation, error, and notification UI;
- explicit permission checks for every privileged setting or power request.

Exit gate: settings survive restart, malformed or future-version data falls
back safely, unprivileged clients are denied, and power UI preserves the
existing shutdown regression path.

## 5H: Installed System Layout

Deliver:

- a Windows-style user-visible installed layout and application-package
  convention without making the kernel depend on user-facing path spelling;
- a documented separation of system files, applications, mutable machine
  state, and per-user data;
- application directories containing executable, manifest, icon, version,
  permissions, and file associations;
- per-user desktop and application settings;
- deterministic discovery and duplicate/version/conflict policy;
- separate developer-image and installed-image assembly rules;
- an update-ready boundary between replaceable system content and preserved
  user data, without claiming a complete updater.

Exit gate: a clean image discovers and launches installed applications without
hard-coded executable lists, while malformed packages and path escapes are
rejected.

## 5I: Fault Injection, Soak, Regression, And Closure

Deliver:

- deterministic failure points across session startup, surface allocation,
  pointer capture, toolkit allocation, PTY transport, file enumeration,
  settings persistence, and package discovery;
- restart and recovery tests for every session service and system application;
- multiwindow keyboard/pointer, terminal, file, and settings workloads on one
  and four vCPUs;
- warmed/final accounting for processes, threads, handles, mappings, surfaces,
  windows, services, PMM, heap, and driver resources;
- a required repeatable 60-second desktop soak and inherited project closure;
- an optional one-hour release soak that is recorded separately.

Exit gate: every regression-matrix row passes, no unexplained resource or lock
drift remains, console recovery is always available, and the roadmap and
history record immutable completion evidence.

## Deferred Work

- separate compositor process and GPU-accelerated composition;
- advanced animation, shadows, color management, and HDR;
- complex-script shaping, full IME, accessibility, and localization;
- clipboard and drag-and-drop beyond any explicitly added bounded baseline;
- multi-user login and remote sessions;
- production package installation, signing, updating, and rollback;
- native GPU, storage, USB, network, and audio drivers;
- the Windows GUI domain and hypervisor end goal.
