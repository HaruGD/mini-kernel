# Phase 5: Desktop Foundation

Phase 5 turns the Phase 4 window stack into a usable native desktop. The
kernel continues to own hardware, isolation, recovery, and resource lifetime;
ordinary user processes render the desktop shell and applications through the
public Window SDK.

The initial compositor remains integrated with `windowd`. Splitting it into a
separate process is a later optimization and isolation project, after the
desktop protocol and recovery behavior are stable.

## Documents

- [Entry baseline](entry_baseline.md): the exact Phase 4.7 closure state and
  remaining desktop gaps.
- [Implementation plan](implementation_plan.md): architecture rules, ordered
  subphases, memory-scaling gate, and exit conditions.
- [Regression matrix](regression_matrix.md): planned contract, QEMU, failure,
  resource, and inherited evidence.
- [Progress ledger](progress.md): live status and immutable implementation
  evidence once work begins.

## Current Status

Phase 5A and 5B are complete. The 5C image, UTF-8 text, drawing, and widget SDK
foundation is implemented, but interactive review reopened its exit gate for
distinct lowercase glyphs, a visible text caret, and client-side responsive
surface/layout replacement after window resize. These 5C.1 items must close
before Phase 5D, the GUI terminal, begins.

## Intended Result

At Phase 5 closure, OS64 boots into a persistent native graphical session in
which the user can:

- move, resize, focus, minimize, maximize, and close windows with a pointer;
- launch applications from a desktop panel and application launcher;
- display validated image assets for cursors, icons, controls, and wallpaper;
- use a graphical terminal backed by a normal user shell;
- browse and modify files with a graphical file manager;
- change bounded system and desktop settings and request shutdown or reboot;
- recover to the kernel console if the graphical session cannot be restored.

The desktop, panel, terminal, file manager, and settings UI are user programs.
They do not become kernel drawing policy.

## Target Runtime Shape

```text
kernel and drivers
  +-- displayd / inputd          system-facing services
  `-- sessiond                   user-session supervisor
       +-- windowd               window server + initial compositor
       +-- desktopd              background, panel, launcher, task list
       +-- terminal.elf          GUI terminal application
       +-- files.elf             GUI file manager
       `-- settings.elf          settings and power UI
```

## Scope Boundary

Phase 5 includes a native desktop foundation, not a finished consumer OS. It
does not require a separate compositor process, GPU acceleration, full Unicode
shaping, JPEG/WebP/GIF/SVG coverage, a production package manager, multi-user
login, networking, audio, USB, native GPU drivers, or the future Windows GUI
domain.

The parallel hardware roadmap may advance independently. A production file
manager eventually needs persistent storage hardware, but its UI and VFS
contracts can first be validated on the existing filesystem stack.
